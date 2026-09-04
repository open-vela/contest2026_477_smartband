#include <nuttx/config.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "openvela_ui_timesync.h"

#define HTTP_TIME_MIN_VALID      1704067200 /* 2024-01-01 00:00:00 UTC */
#define HTTP_TIME_INITIAL_DELAY  25
#define HTTP_TIME_FAST_RETRY     10
#define HTTP_TIME_RETRY_DELAY    60
#define HTTP_TIME_IO_TIMEOUT     5
#define HTTP_TIME_THREAD_STACK   8192

struct http_time_server_s {
    const char *host;
    const char *path;
};

static const struct http_time_server_s g_http_time_servers[] = {
    { "www.baidu.com", "/" },
    { "www.qq.com", "/" },
    { "www.aliyun.com", "/" },
};

static bool http_time_is_valid(time_t value)
{
    /* This board currently uses a signed 32-bit time_t.  A lower bound is
     * sufficient: post-2038 values wrap negative and fail this check. */
    return value >= (time_t)HTTP_TIME_MIN_VALID;
}

static int http_time_send_all(int fd, const char *buffer, size_t length)
{
    size_t sent = 0;

    while (sent < length) {
        ssize_t ret = send(fd, buffer + sent, length - sent, 0);

        if (ret < 0) {
            return -errno;
        }

        if (ret == 0) {
            return -EPIPE;
        }

        sent += (size_t)ret;
    }

    return 0;
}

static int http_time_parse_header(const char *response, time_t *result)
{
    const char *line = response;

    while (*line != '\0') {
        const char *end = strchr(line, '\n');
        size_t length = end == NULL ? strlen(line) : (size_t)(end - line);

        if (length >= 5 && strncasecmp(line, "Date:", 5) == 0) {
            const char *value_start = line + 5;
            const char *value_end = line + length;
            char value[64];
            struct tm tm_value;
            char *parse_end;
            size_t value_length;

            while (value_start < value_end &&
                   (*value_start == ' ' || *value_start == '\t')) {
                value_start++;
            }

            while (value_end > value_start &&
                   (value_end[-1] == '\r' || value_end[-1] == ' ')) {
                value_end--;
            }

            value_length = (size_t)(value_end - value_start);
            if (value_length == 0 || value_length >= sizeof(value)) {
                return -EINVAL;
            }

            memcpy(value, value_start, value_length);
            value[value_length] = '\0';
            memset(&tm_value, 0, sizeof(tm_value));
            parse_end = strptime(value, "%a, %d %b %Y %H:%M:%S GMT",
                                 &tm_value);
            if (parse_end == NULL || *parse_end != '\0') {
                return -EINVAL;
            }

            tm_value.tm_isdst = 0;
            *result = timegm(&tm_value);
            return http_time_is_valid(*result) ? 0 : -ERANGE;
        }

        if (end == NULL || end[1] == '\0') {
            break;
        }

        line = end + 1;
    }

    return -ENODATA;
}

static int http_time_query(const struct http_time_server_s *server,
                           time_t *result)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    struct timeval timeout;
    char request[192];
    char response[1024];
    size_t used = 0;
    int ret;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    ret = getaddrinfo(server->host, "80", &hints, &addresses);
    if (ret != 0) {
        return -EHOSTUNREACH;
    }

    ret = -ECONNREFUSED;
    timeout.tv_sec = HTTP_TIME_IO_TIMEOUT;
    timeout.tv_usec = 0;

    for (address = addresses; address != NULL; address = address->ai_next) {
        int fd = socket(address->ai_family, address->ai_socktype,
                        address->ai_protocol);

        if (fd < 0) {
            ret = -errno;
            continue;
        }

        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

        if (connect(fd, address->ai_addr, address->ai_addrlen) < 0) {
            ret = -errno;
            close(fd);
            continue;
        }

        ret = snprintf(request, sizeof(request),
                       "HEAD %s HTTP/1.0\r\nHost: %s\r\n"
                       "Connection: close\r\n\r\n",
                       server->path, server->host);
        if (ret <= 0 || ret >= (int)sizeof(request)) {
            close(fd);
            ret = -E2BIG;
            break;
        }

        ret = http_time_send_all(fd, request, (size_t)ret);
        if (ret == 0) {
            used = 0;
            while (used < sizeof(response) - 1) {
                ssize_t received = recv(fd, response + used,
                                        sizeof(response) - 1 - used, 0);

                if (received < 0) {
                    ret = -errno;
                    break;
                }

                if (received == 0) {
                    break;
                }

                used += (size_t)received;
                response[used] = '\0';
                if (strstr(response, "\r\n\r\n") != NULL) {
                    break;
                }
            }

            response[used] = '\0';
            if (used > 0) {
                ret = http_time_parse_header(response, result);
            }
        }

        close(fd);
        if (ret == 0) {
            break;
        }
    }

    freeaddrinfo(addresses);
    return ret;
}

static void *http_time_worker(void *arg)
{
    unsigned int attempt = 0;

    (void)arg;
    sleep(HTTP_TIME_INITIAL_DELAY);

    while (!http_time_is_valid(time(NULL))) {
        size_t i;

        attempt++;
        for (i = 0; i < sizeof(g_http_time_servers) /
                        sizeof(g_http_time_servers[0]); i++) {
            struct timespec value;
            int ret;

            ret = http_time_query(&g_http_time_servers[i], &value.tv_sec);
            if (ret != 0) {
                continue;
            }

            value.tv_nsec = 0;
            if (clock_settime(CLOCK_REALTIME, &value) == 0) {
                syslog(LOG_INFO,
                       "openvela_ui: clock synchronized from HTTP Date (%s)\n",
                       g_http_time_servers[i].host);
                return NULL;
            }
        }

        if (attempt == 1 || attempt % 10 == 0) {
            syslog(LOG_WARNING,
                   "openvela_ui: HTTP Date sync unavailable, retrying\n");
        }

        /* Association recovery can take longer than the normal boot path.
         * Retry quickly at first, then back off when Internet access is
         * genuinely unavailable. */
        sleep(attempt < 6 ? HTTP_TIME_FAST_RETRY :
                            HTTP_TIME_RETRY_DELAY);
    }

    return NULL;
}

int openvela_ui_timesync_start(void)
{
    pthread_attr_t attr;
    pthread_t thread;
    int ret;

    if (http_time_is_valid(time(NULL))) {
        return 0;
    }

    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, HTTP_TIME_THREAD_STACK);
    ret = pthread_create(&thread, &attr, http_time_worker, NULL);
    pthread_attr_destroy(&attr);
    if (ret != 0) {
        syslog(LOG_ERR,
               "openvela_ui: failed to start HTTP Date sync: %d\n", ret);
        return -ret;
    }

    pthread_detach(thread);
    return 0;
}
