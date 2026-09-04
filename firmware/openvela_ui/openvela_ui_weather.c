#include <nuttx/config.h>

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netutils/cJSON.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <syslog.h>
#include <unistd.h>

#include "openvela_ui_weather.h"

#define WEATHER_CONFIG_PATH "/data/etc/openvela_ui/weather.conf"
#define WEATHER_DEFAULT_PROXY \
    "http://10.0.2.2:8790/api/weather"
#define WEATHER_PROXY_URL_SIZE 192
#define WEATHER_HTTP_BUFFER_SIZE (16 * 1024)
#define WEATHER_HTTP_PATH_SIZE 768
#define WEATHER_IO_TIMEOUT_SECONDS 10
#define WEATHER_IO_POLL_MILLISECONDS 250
#define WEATHER_THREAD_STACK_SIZE (32 * 1024)

struct weather_request_s {
    uint32_t serial;
    char location_id[16];
    char location[48];
    char administrative_area[64];
};

struct weather_http_url_s {
    char host[96];
    char port[8];
    char path[256];
};

struct weather_service_s {
    pthread_mutex_t lock;
    pthread_cond_t condition;
    bool started;
    bool pending;
    int active_fd;
    uint32_t active_serial;
    uint32_t request_serial;
    uint32_t revision;
    char proxy_url[WEATHER_PROXY_URL_SIZE];
    struct weather_request_s request;
    struct openvela_ui_weather_snapshot_s snapshot;
};

static struct weather_service_s g_weather = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .condition = PTHREAD_COND_INITIALIZER,
    .active_fd = -1,
};

static void weather_copy(char *destination, size_t size,
                         const char *source)
{
    if (size == 0) {
        return;
    }
    if (source == destination) {
        return;
    }

    snprintf(destination, size, "%s", source ? source : "");
}

static char *weather_trim(char *text)
{
    char *end;

    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }

    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return text;
}

static void weather_load_config(void)
{
    FILE *file;
    char line[256];

    weather_copy(g_weather.proxy_url, sizeof(g_weather.proxy_url),
                 WEATHER_DEFAULT_PROXY);
    file = fopen(WEATHER_CONFIG_PATH, "r");
    if (file == NULL) {
        syslog(LOG_WARNING,
               "openvela_ui: weather proxy config missing; using emulator default\n");
        return;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *key = weather_trim(line);
        char *separator;
        char *value;

        if (*key == '\0' || *key == '#') {
            continue;
        }

        separator = strchr(key, '=');
        if (separator == NULL) {
            continue;
        }
        *separator = '\0';
        value = weather_trim(separator + 1);
        key = weather_trim(key);
        if (strcmp(key, "proxy_url") == 0 && *value != '\0') {
            weather_copy(g_weather.proxy_url,
                         sizeof(g_weather.proxy_url), value);
        }
    }

    fclose(file);
}

static int weather_parse_http_url(const char *url,
                                  struct weather_http_url_s *result)
{
    static const char prefix[] = "http://";
    const char *authority;
    const char *path;
    const char *colon;
    size_t authority_length;
    size_t host_length;

    if (strncmp(url, prefix, sizeof(prefix) - 1) != 0) {
        return -EPROTONOSUPPORT;
    }

    authority = url + sizeof(prefix) - 1;
    path = strchr(authority, '/');
    authority_length = path ? (size_t)(path - authority) : strlen(authority);
    if (authority_length == 0 || authority_length >= sizeof(result->host)) {
        return -EINVAL;
    }

    colon = memchr(authority, ':', authority_length);
    if (colon != NULL) {
        size_t port_length;

        host_length = (size_t)(colon - authority);
        port_length = authority_length - host_length - 1;
        if (host_length == 0 || port_length == 0 ||
            port_length >= sizeof(result->port)) {
            return -EINVAL;
        }
        memcpy(result->port, colon + 1, port_length);
        result->port[port_length] = '\0';
    } else {
        host_length = authority_length;
        weather_copy(result->port, sizeof(result->port), "80");
    }

    memcpy(result->host, authority, host_length);
    result->host[host_length] = '\0';
    weather_copy(result->path, sizeof(result->path),
                 path && *path ? path : "/");
    return 0;
}

static int weather_url_encode(const char *source, char *destination,
                              size_t size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0;

    while (*source != '\0') {
        unsigned char value = (unsigned char)*source++;
        bool plain = (value >= 'a' && value <= 'z') ||
                     (value >= 'A' && value <= 'Z') ||
                     (value >= '0' && value <= '9') ||
                     value == '-' || value == '_' || value == '.' ||
                     value == '~';

        if (plain) {
            if (used + 1 >= size) {
                return -E2BIG;
            }
            destination[used++] = (char)value;
        } else {
            if (used + 3 >= size) {
                return -E2BIG;
            }
            destination[used++] = '%';
            destination[used++] = hex[value >> 4];
            destination[used++] = hex[value & 0x0f];
        }
    }

    destination[used] = '\0';
    return 0;
}

static bool weather_request_is_current(uint32_t serial)
{
    bool current;

    pthread_mutex_lock(&g_weather.lock);
    current = serial == g_weather.request_serial;
    pthread_mutex_unlock(&g_weather.lock);
    return current;
}

static int weather_activate_socket(int fd, uint32_t serial)
{
    int ret = 0;

    pthread_mutex_lock(&g_weather.lock);
    if (serial != g_weather.request_serial) {
        ret = -ECANCELED;
    } else {
        g_weather.active_fd = fd;
        g_weather.active_serial = serial;
    }
    pthread_mutex_unlock(&g_weather.lock);
    return ret;
}

static void weather_close_socket(int fd, uint32_t serial)
{
    pthread_mutex_lock(&g_weather.lock);
    if (g_weather.active_fd == fd && g_weather.active_serial == serial) {
        g_weather.active_fd = -1;
        g_weather.active_serial = 0;
    }
    pthread_mutex_unlock(&g_weather.lock);
    close(fd);
}

static int weather_wait_socket(int fd, short events, uint32_t serial)
{
    struct pollfd descriptor;
    int elapsed = 0;

    descriptor.fd = fd;
    descriptor.events = events;
    while (elapsed < WEATHER_IO_TIMEOUT_SECONDS * 1000) {
        int ret;

        if (!weather_request_is_current(serial)) {
            return -ECANCELED;
        }

        descriptor.revents = 0;
        ret = poll(&descriptor, 1, WEATHER_IO_POLL_MILLISECONDS);
        if (ret > 0) {
            if (!weather_request_is_current(serial)) {
                return -ECANCELED;
            }
            if ((descriptor.revents & POLLNVAL) != 0) {
                return -EBADF;
            }
            if ((descriptor.revents & (events | POLLERR | POLLHUP)) != 0) {
                return 0;
            }
        } else if (ret < 0 && errno != EINTR) {
            return -errno;
        }

        elapsed += WEATHER_IO_POLL_MILLISECONDS;
    }

    return -ETIMEDOUT;
}

static int weather_send_all(int fd, uint32_t serial,
                            const char *buffer, size_t length)
{
    size_t sent = 0;

    while (sent < length) {
        ssize_t ret;

        if (!weather_request_is_current(serial)) {
            return -ECANCELED;
        }

        ret = send(fd, buffer + sent, length - sent, 0);

        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                int wait_ret = weather_wait_socket(fd, POLLOUT, serial);

                if (wait_ret < 0) {
                    return wait_ret;
                }
                continue;
            }
            return -errno;
        }
        if (ret == 0) {
            return -EPIPE;
        }
        sent += (size_t)ret;
    }

    return 0;
}

static int weather_connect(const struct weather_http_url_s *url,
                           uint32_t serial)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    struct timeval timeout;
    int fd = -1;
    int last_error = -ECONNREFUSED;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(url->host, url->port, &hints, &addresses) != 0) {
        return -EHOSTUNREACH;
    }

    timeout.tv_sec = WEATHER_IO_TIMEOUT_SECONDS;
    timeout.tv_usec = 0;
    for (address = addresses; address != NULL; address = address->ai_next) {
        int flags;

        fd = socket(address->ai_family, address->ai_socktype,
                    address->ai_protocol);
        if (fd < 0) {
            continue;
        }

        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        flags = fcntl(fd, F_GETFL, 0);
        if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            last_error = -errno;
            close(fd);
            fd = -1;
            continue;
        }
        if (weather_activate_socket(fd, serial) < 0) {
            close(fd);
            fd = -1;
            last_error = -ECANCELED;
            break;
        }

        if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) {
            break;
        }

        if (errno == EINPROGRESS || errno == EAGAIN) {
            int ret = weather_wait_socket(fd, POLLOUT, serial);

            if (ret == 0) {
                int socket_error = 0;
                socklen_t error_size = sizeof(socket_error);

                if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socket_error,
                               &error_size) < 0) {
                    ret = -errno;
                } else if (socket_error != 0) {
                    ret = -socket_error;
                }
            }
            if (ret == 0) {
                break;
            }
            last_error = ret;
        } else {
            last_error = -errno;
        }

        weather_close_socket(fd, serial);
        fd = -1;
        if (last_error == -ECANCELED) {
            break;
        }
    }

    freeaddrinfo(addresses);
    return fd >= 0 ? fd : last_error;
}

static int weather_http_get(const struct weather_request_s *request,
                            char *response, size_t response_size,
                            char **body)
{
    struct weather_http_url_s url;
    char location_id[64];
    char location[160];
    char administrative_area[224];
    char path[WEATHER_HTTP_PATH_SIZE];
    char header[1152];
    char *header_end;
    size_t used = 0;
    int fd;
    int status;
    int length;
    int ret;

    ret = weather_parse_http_url(g_weather.proxy_url, &url);
    if (ret < 0) {
        return ret;
    }
    if (weather_url_encode(request->location_id, location_id,
                           sizeof(location_id)) < 0 ||
        weather_url_encode(request->location, location,
                           sizeof(location)) < 0 ||
        weather_url_encode(request->administrative_area,
                           administrative_area,
                           sizeof(administrative_area)) < 0) {
        return -E2BIG;
    }

    length = snprintf(path, sizeof(path), "%s%clocationId=%s&location=%s&adm=%s",
                      url.path, strchr(url.path, '?') ? '&' : '?',
                      location_id, location, administrative_area);
    if (length <= 0 || length >= (int)sizeof(path)) {
        return -E2BIG;
    }

    length = snprintf(header, sizeof(header),
                      "GET %s HTTP/1.0\r\nHost: %s%s%s\r\n"
                      "Accept: application/json\r\n"
                      "Accept-Encoding: identity\r\n"
                      "Connection: close\r\n\r\n",
                      path, url.host,
                      strcmp(url.port, "80") == 0 ? "" : ":",
                      strcmp(url.port, "80") == 0 ? "" : url.port);
    if (length <= 0 || length >= (int)sizeof(header)) {
        return -E2BIG;
    }

    fd = weather_connect(&url, request->serial);
    if (fd < 0) {
        return fd;
    }

    ret = weather_send_all(fd, request->serial, header, (size_t)length);
    while (ret == 0 && used < response_size - 1) {
        ssize_t received;

        if (!weather_request_is_current(request->serial)) {
            ret = -ECANCELED;
            break;
        }

        received = recv(fd, response + used,
                        response_size - 1 - used, 0);

        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                ret = weather_wait_socket(fd, POLLIN, request->serial);
                continue;
            }
            ret = -errno;
            break;
        }
        if (received == 0) {
            break;
        }
        used += (size_t)received;
    }
    weather_close_socket(fd, request->serial);
    if (ret < 0) {
        return ret;
    }
    if (used == response_size - 1) {
        return -EFBIG;
    }

    response[used] = '\0';
    if (sscanf(response, "HTTP/%*u.%*u %d", &status) != 1) {
        return -EBADMSG;
    }
    if (status != 200) {
        if (status == 400) {
            return -EINVAL;
        }
        if (status == 401 || status == 403) {
            return -EACCES;
        }
        if (status == 404) {
            return -ENOENT;
        }
        if (status == 408 || status == 504) {
            return -ETIMEDOUT;
        }
        if (status == 429 || status == 502 || status == 503) {
            return -EAGAIN;
        }
        if (status >= 500) {
            return -EHOSTUNREACH;
        }
        return -EIO;
    }
    header_end = strstr(response, "\r\n\r\n");
    if (header_end == NULL) {
        return -EBADMSG;
    }
    *body = header_end + 4;
    return 0;
}

static const char *weather_json_string(const cJSON *object,
                                       const char *name,
                                       const char *fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

    return cJSON_IsString(item) && item->valuestring != NULL ?
           item->valuestring : fallback;
}

static void weather_parse_location(
    const cJSON *location,
    struct openvela_ui_weather_snapshot_s *result)
{
    const char *name = weather_json_string(location, "displayName", NULL);
    const char *adm2 = weather_json_string(location, "displayAdm2", NULL);
    const char *adm1 = weather_json_string(location, "displayAdm1", NULL);

    if (name == NULL || *name == '\0') {
        name = weather_json_string(location, "name", result->location_name);
    }
    if (adm2 == NULL || *adm2 == '\0') {
        adm2 = weather_json_string(location, "adm2", "");
    }
    if (adm1 == NULL || *adm1 == '\0') {
        adm1 = weather_json_string(location, "adm1", "");
    }

    weather_copy(result->location_id, sizeof(result->location_id),
                 weather_json_string(location, "id", result->location_id));
    weather_copy(result->location_name, sizeof(result->location_name), name);
    if (*adm2 != '\0' && strcmp(adm2, adm1) != 0) {
        snprintf(result->administrative_area,
                 sizeof(result->administrative_area), "%s · %s", adm2, adm1);
    } else {
        weather_copy(result->administrative_area,
                     sizeof(result->administrative_area),
                     *adm2 != '\0' ? adm2 : adm1);
    }
    weather_copy(result->country, sizeof(result->country),
                 weather_json_string(location, "country", "中国"));
}

static int weather_parse_json(
    const struct weather_request_s *request, const char *body,
    struct openvela_ui_weather_snapshot_s *result)
{
    cJSON *root = cJSON_Parse(body);
    const cJSON *live;
    const cJSON *now;
    const cJSON *location;
    const cJSON *forecast;
    int index;
    int ret = -EBADMSG;

    if (root == NULL) {
        return -EBADMSG;
    }

    live = cJSON_GetObjectItemCaseSensitive(root, "live");
    now = cJSON_GetObjectItemCaseSensitive(root, "now");
    location = cJSON_GetObjectItemCaseSensitive(root, "location");
    forecast = cJSON_GetObjectItemCaseSensitive(root, "forecast");
    if (strcmp(weather_json_string(root, "code", ""), "200") != 0 ||
        !cJSON_IsTrue(live) || !cJSON_IsObject(now) ||
        !cJSON_IsObject(location) || !cJSON_IsArray(forecast)) {
        goto out;
    }

    memset(result, 0, sizeof(*result));
    result->state = OPENVELA_UI_WEATHER_READY;
    result->has_data = true;
    weather_copy(result->location_id, sizeof(result->location_id),
                 request->location_id);
    weather_copy(result->location_name, sizeof(result->location_name),
                 request->location);
    weather_copy(result->administrative_area,
                 sizeof(result->administrative_area),
                 request->administrative_area);
    weather_parse_location(location, result);
    weather_copy(result->updated_at, sizeof(result->updated_at),
                 weather_json_string(root, "updatedAt", ""));
    weather_copy(result->observed_at, sizeof(result->observed_at),
                 weather_json_string(root, "observedAt", ""));
    weather_copy(result->temperature, sizeof(result->temperature),
                 weather_json_string(now, "temp", "--"));
    weather_copy(result->feels_like, sizeof(result->feels_like),
                 weather_json_string(now, "feelsLike", "--"));
    weather_copy(result->humidity, sizeof(result->humidity),
                 weather_json_string(now, "humidity", "--"));
    weather_copy(result->visibility, sizeof(result->visibility),
                 weather_json_string(now, "visibility", "--"));
    weather_copy(result->text, sizeof(result->text),
                 weather_json_string(now, "text", "--"));
    weather_copy(result->icon, sizeof(result->icon),
                 weather_json_string(now, "icon", "999"));

    for (index = 0; index < OPENVELA_UI_WEATHER_FORECAST_DAYS; index++) {
        const cJSON *item = cJSON_GetArrayItem(forecast, index);
        struct openvela_ui_weather_day_s *day = &result->forecast[index];

        weather_copy(day->weekday, sizeof(day->weekday), "--");
        weather_copy(day->text, sizeof(day->text), "--");
        weather_copy(day->icon, sizeof(day->icon), "999");
        weather_copy(day->minimum, sizeof(day->minimum), "--");
        weather_copy(day->maximum, sizeof(day->maximum), "--");
        if (!cJSON_IsObject(item)) {
            continue;
        }
        weather_copy(day->weekday, sizeof(day->weekday),
                     weather_json_string(item, "weekday", "--"));
        weather_copy(day->text, sizeof(day->text),
                     weather_json_string(item, "text", "--"));
        weather_copy(day->icon, sizeof(day->icon),
                     weather_json_string(item, "icon", "999"));
        weather_copy(day->minimum, sizeof(day->minimum),
                     weather_json_string(item, "min", "--"));
        weather_copy(day->maximum, sizeof(day->maximum),
                     weather_json_string(item, "max", "--"));
    }

    ret = 0;
out:
    cJSON_Delete(root);
    return ret;
}

static void *weather_worker(void *argument)
{
    char *response;

    (void)argument;
    response = malloc(WEATHER_HTTP_BUFFER_SIZE);
    if (response == NULL) {
        syslog(LOG_ERR, "openvela_ui: weather worker has no response buffer\n");
    }

    for (;;) {
        struct weather_request_s request;
        struct openvela_ui_weather_snapshot_s result;
        char *body = NULL;
        int ret;

        pthread_mutex_lock(&g_weather.lock);
        while (!g_weather.pending) {
            pthread_cond_wait(&g_weather.condition, &g_weather.lock);
        }
        request = g_weather.request;
        g_weather.pending = false;
        pthread_mutex_unlock(&g_weather.lock);

        if (response == NULL) {
            ret = -ENOMEM;
        } else {
            ret = weather_http_get(&request, response,
                                   WEATHER_HTTP_BUFFER_SIZE, &body);
            if (ret == 0) {
                ret = weather_parse_json(&request, body, &result);
            }
        }

        pthread_mutex_lock(&g_weather.lock);
        if (request.serial == g_weather.request_serial) {
            if (ret == 0) {
                result.revision = ++g_weather.revision;
                g_weather.snapshot = result;
                syslog(LOG_INFO,
                       "openvela_ui: weather updated for %s\n",
                       result.location_name);
            } else {
                g_weather.snapshot.state = OPENVELA_UI_WEATHER_ERROR;
                g_weather.snapshot.error = ret;
                g_weather.snapshot.revision = ++g_weather.revision;
                syslog(LOG_WARNING,
                       "openvela_ui: weather request failed: %d\n", ret);
            }
        }
        pthread_mutex_unlock(&g_weather.lock);
    }

    return NULL;
}

int openvela_ui_weather_start(void)
{
    pthread_attr_t attributes;
    pthread_t thread;
    int ret;

    pthread_mutex_lock(&g_weather.lock);
    if (g_weather.started) {
        pthread_mutex_unlock(&g_weather.lock);
        return 0;
    }
    weather_load_config();
    g_weather.started = true;
    pthread_mutex_unlock(&g_weather.lock);

    pthread_attr_init(&attributes);
    pthread_attr_setstacksize(&attributes, WEATHER_THREAD_STACK_SIZE);
    ret = pthread_create(&thread, &attributes, weather_worker, NULL);
    pthread_attr_destroy(&attributes);
    if (ret != 0) {
        pthread_mutex_lock(&g_weather.lock);
        g_weather.started = false;
        g_weather.snapshot.state = OPENVELA_UI_WEATHER_ERROR;
        g_weather.snapshot.error = -ret;
        g_weather.snapshot.revision = ++g_weather.revision;
        pthread_mutex_unlock(&g_weather.lock);
        syslog(LOG_ERR,
               "openvela_ui: failed to start weather worker: %d\n", ret);
        return -ret;
    }

    pthread_detach(thread);
    return 0;
}

int openvela_ui_weather_request(const char *location_id,
                                const char *location,
                                const char *administrative_area)
{
    bool same_location;

    if (location == NULL || *location == '\0') {
        return -EINVAL;
    }
    if (openvela_ui_weather_start() < 0) {
        return -EIO;
    }

    pthread_mutex_lock(&g_weather.lock);
    same_location = strcmp(g_weather.snapshot.location_id,
                           location_id ? location_id : "") == 0 &&
                    strcmp(g_weather.snapshot.location_name, location) == 0;
    g_weather.request_serial++;
    if (g_weather.active_fd >= 0 &&
        g_weather.active_serial != g_weather.request_serial) {
        shutdown(g_weather.active_fd, SHUT_RDWR);
    }
    g_weather.request.serial = g_weather.request_serial;
    weather_copy(g_weather.request.location_id,
                 sizeof(g_weather.request.location_id), location_id);
    weather_copy(g_weather.request.location,
                 sizeof(g_weather.request.location), location);
    weather_copy(g_weather.request.administrative_area,
                 sizeof(g_weather.request.administrative_area),
                 administrative_area);
    if (!same_location) {
        memset(&g_weather.snapshot, 0, sizeof(g_weather.snapshot));
        weather_copy(g_weather.snapshot.location_id,
                     sizeof(g_weather.snapshot.location_id), location_id);
        weather_copy(g_weather.snapshot.location_name,
                     sizeof(g_weather.snapshot.location_name), location);
        weather_copy(g_weather.snapshot.administrative_area,
                     sizeof(g_weather.snapshot.administrative_area),
                     administrative_area);
    }
    g_weather.snapshot.state = OPENVELA_UI_WEATHER_LOADING;
    g_weather.snapshot.error = 0;
    g_weather.snapshot.revision = ++g_weather.revision;
    g_weather.pending = true;
    pthread_cond_signal(&g_weather.condition);
    pthread_mutex_unlock(&g_weather.lock);
    return 0;
}

int openvela_ui_weather_snapshot(
    struct openvela_ui_weather_snapshot_s *snapshot)
{
    if (snapshot == NULL) {
        return -EINVAL;
    }

    pthread_mutex_lock(&g_weather.lock);
    *snapshot = g_weather.snapshot;
    pthread_mutex_unlock(&g_weather.lock);
    return 0;
}
