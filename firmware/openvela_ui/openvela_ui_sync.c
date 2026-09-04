#include <nuttx/config.h>

#include <curl/curl.h>
#include <dirent.h>
#include <errno.h>
#include <netutils/cJSON.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "openvela_ui_sync.h"

#define SYNC_PROTOCOL "smart-band-daily-sync"
#define SYNC_VERSION 1
#define SYNC_MESSAGE_TYPE "daily_activity_health"

#define SYNC_CONFIG_PATH "/data/etc/openvela_ui/sync.conf"
#define SYNC_ROOT "/data/etc/openvela_ui/sync"
#define SYNC_HEALTH_ROOT SYNC_ROOT "/health"
#define SYNC_OUTBOX_ROOT SYNC_ROOT "/outbox"
#define SYNC_STATE_PATH SYNC_ROOT "/state.conf"
#define SYNC_STATE_TMP_PATH SYNC_ROOT "/state.tmp"
#define SYNC_SPORT_STATE_PATH "/data/etc/openvela_ui/sport_state.conf"

#define SYNC_DEFAULT_ENDPOINT "http://10.0.2.2:8790/api/sync/frame"
#define SYNC_DEFAULT_CONTROL "http://10.0.2.2:8790/api/sync/control"
#define SYNC_DEFAULT_DEVICE_ID "openvela-native-01"
#define SYNC_CA_PRIMARY "/etc/ssl/certs/ca-certificates.crt"
#define SYNC_CA_RESOURCE "/resource/etc/ssl/curl/ca-certificates.crt"
#define SYNC_RESOLV_CONFIG "/tmp/resolv.conf"

#define SYNC_URL_SIZE 256
#define SYNC_DEVICE_ID_SIZE 64
#define SYNC_TOKEN_SIZE 160
#define SYNC_DAY_SIZE 9
#define SYNC_DASHED_DAY_SIZE 11
#define SYNC_PATH_SIZE 256
#define SYNC_HTTP_RESPONSE_SIZE (8 * 1024)
#define SYNC_MAX_DOCUMENT_SIZE (64 * 1024)
#define SYNC_MAX_HEALTH_FILE_SIZE (32 * 1024)
#define SYNC_MAX_HEALTH_RECORDS 256
#define SYNC_MAX_SYNCED_DAYS 30
#define SYNC_MAX_SIMULATION_OUTBOX 4
#define SYNC_MAX_SPORT_HISTORY 8
#define SYNC_HOURLY_SAMPLES 24
#define SYNC_HEALTH_QUEUE_SIZE 16
#define SYNC_CHUNK_SIZE 768
#define SYNC_WORKER_INTERVAL_SECONDS 15
#define SYNC_CONTROL_INTERVAL_SECONDS 15
#define SYNC_CONTROL_ERROR_LOG_SECONDS 60
#define SYNC_RETRY_INITIAL_SECONDS 30
#define SYNC_RETRY_MAX_SECONDS (15 * 60)
#define SYNC_MAX_SAFE_REQUEST_ID 9007199254740991.0
#define SYNC_THREAD_STACK_SIZE (96 * 1024)
#define SYNC_VALID_YEAR 2020

enum sync_health_type_e {
    SYNC_HEALTH_HEART_RATE = 1,
    SYNC_HEALTH_BLOOD_PRESSURE = 2,
};

struct sync_health_event_s {
    enum sync_health_type_e type;
    time_t measured_at;
    uint16_t primary;
    uint16_t secondary;
    uint16_t pulse;
};

struct sync_config_s {
    bool enabled;
    char endpoint[SYNC_URL_SIZE];
    char control_url[SYNC_URL_SIZE];
    char device_id[SYNC_DEVICE_ID_SIZE];
    char token[SYNC_TOKEN_SIZE];
};

struct sync_state_s {
    char activated_day[SYNC_DAY_SIZE];
    uint64_t last_control_id;
    size_t synced_count;
    char synced_days[SYNC_MAX_SYNCED_DAYS][SYNC_DAY_SIZE];
};

struct sync_sport_day_s {
    bool valid;
    bool hours_valid;
    char day[SYNC_DAY_SIZE];
    uint32_t steps;
    uint32_t goals[3];
    uint32_t hours[SYNC_HOURLY_SAMPLES];
};

struct sync_sport_file_s {
    struct sync_sport_day_s current;
    struct sync_sport_day_s history[SYNC_MAX_SPORT_HISTORY];
    size_t history_count;
};

struct sync_http_response_s {
    char data[SYNC_HTTP_RESPONSE_SIZE];
    size_t length;
};

struct sync_service_s {
    pthread_mutex_t lock;
    pthread_cond_t condition;
    pthread_t thread;
    bool started;
    bool stopping;
    struct sync_health_event_s queue[SYNC_HEALTH_QUEUE_SIZE];
    size_t queue_head;
    size_t queue_count;
};

static struct sync_service_s g_sync = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .condition = PTHREAD_COND_INITIALIZER,
};

static void sync_copy(char *destination, size_t size, const char *source)
{
    if (size == 0 || destination == source) {
        return;
    }
    snprintf(destination, size, "%s", source ? source : "");
}

static char *sync_trim(char *text)
{
    char *end;

    while (*text == ' ' || *text == '\t' || *text == '\r' ||
           *text == '\n') {
        text++;
    }
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t' ||
                          end[-1] == '\r' || end[-1] == '\n')) {
        end--;
    }
    *end = '\0';
    return text;
}

static bool sync_parse_bool(const char *value, bool fallback)
{
    if (!value) {
        return fallback;
    }
    if (strcmp(value, "1") == 0 || strcasecmp(value, "true") == 0 ||
        strcasecmp(value, "yes") == 0 || strcasecmp(value, "on") == 0) {
        return true;
    }
    if (strcmp(value, "0") == 0 || strcasecmp(value, "false") == 0 ||
        strcasecmp(value, "no") == 0 || strcasecmp(value, "off") == 0) {
        return false;
    }
    return fallback;
}

static void sync_load_config(struct sync_config_s *config)
{
    FILE *file;
    char line[384];

    memset(config, 0, sizeof(*config));
    config->enabled = true;
    sync_copy(config->endpoint, sizeof(config->endpoint),
              SYNC_DEFAULT_ENDPOINT);
    sync_copy(config->control_url, sizeof(config->control_url),
              SYNC_DEFAULT_CONTROL);
    sync_copy(config->device_id, sizeof(config->device_id),
              SYNC_DEFAULT_DEVICE_ID);

    file = fopen(SYNC_CONFIG_PATH, "r");
    if (!file) {
        syslog(LOG_WARNING,
               "openvela_ui: sync config missing; using emulator default\n");
        return;
    }

    while (fgets(line, sizeof(line), file)) {
        char *key = sync_trim(line);
        char *separator;
        char *value;

        if (*key == '\0' || *key == '#') {
            continue;
        }
        separator = strchr(key, '=');
        if (!separator) {
            continue;
        }
        *separator = '\0';
        value = sync_trim(separator + 1);
        key = sync_trim(key);
        if (strcmp(key, "enabled") == 0) {
            config->enabled = sync_parse_bool(value, config->enabled);
        } else if (strcmp(key, "endpoint") == 0 && *value) {
            sync_copy(config->endpoint, sizeof(config->endpoint), value);
        } else if (strcmp(key, "control_url") == 0 && *value) {
            sync_copy(config->control_url, sizeof(config->control_url),
                      value);
        } else if (strcmp(key, "device_id") == 0 && *value) {
            sync_copy(config->device_id, sizeof(config->device_id), value);
        } else if (strcmp(key, "token") == 0 && *value) {
            sync_copy(config->token, sizeof(config->token), value);
        }
    }
    fclose(file);
}

static void sync_ensure_directories(void)
{
    mkdir("/data/etc", 0777);
    mkdir("/data/etc/openvela_ui", 0777);
    mkdir(SYNC_ROOT, 0777);
    mkdir(SYNC_HEALTH_ROOT, 0777);
    mkdir(SYNC_OUTBOX_ROOT, 0777);
}

static bool sync_url_needs_dns(const char *url)
{
    const char *host = strstr(url ? url : "", "://");
    const char *cursor;

    if (!host) {
        return true;
    }
    host += 3;
    if (*host == '[') {
        return false;
    }
    for (cursor = host; *cursor && *cursor != '/' && *cursor != ':' &&
                        *cursor != '?' && *cursor != '#'; cursor++) {
        if ((*cursor < '0' || *cursor > '9') && *cursor != '.') {
            return true;
        }
    }
    return cursor == host;
}

static bool sync_resolver_ready(const struct sync_config_s *config)
{
    FILE *file;
    char line[128];
    bool ready = false;

    if (!sync_url_needs_dns(config->control_url) &&
        !sync_url_needs_dns(config->endpoint)) {
        return true;
    }
    file = fopen(SYNC_RESOLV_CONFIG, "r");
    if (!file) {
        return false;
    }
    while (fgets(line, sizeof(line), file)) {
        char *text = sync_trim(line);

        if (strncmp(text, "nameserver", 10) == 0 &&
            (text[10] == ' ' || text[10] == '\t')) {
            ready = true;
            break;
        }
    }
    fclose(file);
    return ready;
}

static bool sync_day_valid(const char *day)
{
    int index;
    int year;
    int month;
    int date;

    if (!day || strlen(day) != 8) {
        return false;
    }
    for (index = 0; index < 8; index++) {
        if (day[index] < '0' || day[index] > '9') {
            return false;
        }
    }
    year = (day[0] - '0') * 1000 + (day[1] - '0') * 100 +
           (day[2] - '0') * 10 + day[3] - '0';
    month = (day[4] - '0') * 10 + day[5] - '0';
    date = (day[6] - '0') * 10 + day[7] - '0';
    return year >= SYNC_VALID_YEAR && year <= 9999 &&
           month >= 1 && month <= 12 && date >= 1 && date <= 31;
}

static bool sync_day_for_time(time_t timestamp, char day[SYNC_DAY_SIZE])
{
    struct tm local;
    int year;

    if (!localtime_r(&timestamp, &local)) {
        day[0] = '\0';
        return false;
    }
    year = local.tm_year + 1900;
    if (year < SYNC_VALID_YEAR || year > 9999) {
        day[0] = '\0';
        return false;
    }
    snprintf(day, SYNC_DAY_SIZE, "%04d%02d%02d", year,
             local.tm_mon + 1, local.tm_mday);
    return true;
}

static bool sync_today(char day[SYNC_DAY_SIZE])
{
    return sync_day_for_time(time(NULL), day);
}

static void sync_day_dashed(const char day[SYNC_DAY_SIZE],
                            char dashed[SYNC_DASHED_DAY_SIZE])
{
    snprintf(dashed, SYNC_DASHED_DAY_SIZE, "%.4s-%.2s-%.2s",
             day, day + 4, day + 6);
}

static bool sync_day_before(int days_before, char day[SYNC_DAY_SIZE])
{
    time_t timestamp = time(NULL) - (time_t)days_before * 24 * 60 * 60;

    return sync_day_for_time(timestamp, day);
}

static bool sync_next_day(const char day[SYNC_DAY_SIZE],
                          char next[SYNC_DAY_SIZE])
{
    struct tm local;
    time_t timestamp;

    if (!sync_day_valid(day)) {
        return false;
    }
    memset(&local, 0, sizeof(local));
    local.tm_year = ((day[0] - '0') * 1000 + (day[1] - '0') * 100 +
                     (day[2] - '0') * 10 + day[3] - '0') - 1900;
    local.tm_mon = (day[4] - '0') * 10 + day[5] - '0' - 1;
    local.tm_mday = (day[6] - '0') * 10 + day[7] - '0';
    local.tm_hour = 12;
    local.tm_isdst = -1;
    timestamp = mktime(&local);
    if (timestamp == (time_t)-1) {
        return false;
    }
    timestamp += 24 * 60 * 60;
    return sync_day_for_time(timestamp, next);
}

static int sync_write_atomic(const char *path, const char *temporary,
                             const char *data, size_t length)
{
    FILE *file;
    size_t written;
    int flush_result = 0;
    int sync_result = 0;
    int close_result;

    file = fopen(temporary, "w");
    if (!file) {
        return -errno;
    }
    written = fwrite(data, 1, length, file);
    if (written == length) {
        flush_result = fflush(file);
        if (flush_result == 0) {
            sync_result = fsync(fileno(file));
        }
    }
    close_result = fclose(file);
    if (written != length || flush_result != 0 || sync_result != 0 ||
        close_result != 0) {
        unlink(temporary);
        return -EIO;
    }
    if (rename(temporary, path) != 0) {
        int error = errno;

        unlink(temporary);
        return -error;
    }
    return 0;
}

static char *sync_read_file(const char *path, size_t maximum,
                            size_t *length)
{
    FILE *file;
    char *buffer;
    long size;
    size_t read_size;

    if (length) {
        *length = 0;
    }
    file = fopen(path, "r");
    if (!file) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    size = ftell(file);
    if (size < 0 || (size_t)size > maximum ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    buffer = malloc((size_t)size + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }
    read_size = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    if (read_size != (size_t)size) {
        free(buffer);
        return NULL;
    }
    buffer[read_size] = '\0';
    if (length) {
        *length = read_size;
    }
    return buffer;
}

static bool sync_state_has_day(const struct sync_state_s *state,
                               const char *day)
{
    size_t index;

    for (index = 0; index < state->synced_count; index++) {
        if (strcmp(state->synced_days[index], day) == 0) {
            return true;
        }
    }
    return false;
}

static void sync_state_add_day(struct sync_state_s *state, const char *day)
{
    if (!sync_day_valid(day) || sync_state_has_day(state, day)) {
        return;
    }
    if (state->synced_count == SYNC_MAX_SYNCED_DAYS) {
        memmove(state->synced_days, state->synced_days + 1,
                sizeof(state->synced_days[0]) *
                (SYNC_MAX_SYNCED_DAYS - 1));
        state->synced_count--;
    }
    sync_copy(state->synced_days[state->synced_count],
              sizeof(state->synced_days[state->synced_count]), day);
    state->synced_count++;
}

static void sync_state_prune(struct sync_state_s *state, const char *cutoff)
{
    size_t source;
    size_t destination = 0;

    for (source = 0; source < state->synced_count; source++) {
        if (strcmp(state->synced_days[source], cutoff) >= 0) {
            if (destination != source) {
                memcpy(state->synced_days[destination],
                       state->synced_days[source], SYNC_DAY_SIZE);
            }
            destination++;
        }
    }
    state->synced_count = destination;
}

static void sync_state_load(struct sync_state_s *state)
{
    FILE *file;
    char line[128];

    memset(state, 0, sizeof(*state));
    file = fopen(SYNC_STATE_PATH, "r");
    if (!file) {
        return;
    }
    while (fgets(line, sizeof(line), file)) {
        char text[64];
        unsigned long long identifier;

        if (sscanf(line, "activated_day=%8s", text) == 1 &&
            sync_day_valid(text)) {
            sync_copy(state->activated_day,
                      sizeof(state->activated_day), text);
        } else if (sscanf(line, "last_control_id=%llu", &identifier) == 1) {
            state->last_control_id = (uint64_t)identifier;
        } else if (sscanf(line, "synced=%8s", text) == 1 &&
                   sync_day_valid(text)) {
            sync_state_add_day(state, text);
        }
    }
    fclose(file);
}

static int sync_state_save(const struct sync_state_s *state)
{
    char *buffer;
    size_t capacity = 2048;
    size_t used = 0;
    size_t index;
    int length;
    int result;

    buffer = malloc(capacity);
    if (!buffer) {
        return -ENOMEM;
    }
    length = snprintf(buffer, capacity,
                      "activated_day=%s\nlast_control_id=%llu\n",
                      state->activated_day,
                      (unsigned long long)state->last_control_id);
    if (length < 0 || (size_t)length >= capacity) {
        free(buffer);
        return -E2BIG;
    }
    used = (size_t)length;
    for (index = 0; index < state->synced_count; index++) {
        length = snprintf(buffer + used, capacity - used, "synced=%s\n",
                          state->synced_days[index]);
        if (length < 0 || (size_t)length >= capacity - used) {
            free(buffer);
            return -E2BIG;
        }
        used += (size_t)length;
    }
    result = sync_write_atomic(SYNC_STATE_PATH, SYNC_STATE_TMP_PATH,
                               buffer, used);
    free(buffer);
    return result;
}

static int sync_persist_health(const struct sync_health_event_s *event)
{
    char day[SYNC_DAY_SIZE];
    char path[SYNC_PATH_SIZE];
    char temporary[SYNC_PATH_SIZE];
    char line[128];
    char *existing = NULL;
    char *combined;
    size_t existing_length = 0;
    size_t line_length;
    struct stat information;
    int result;

    if (!sync_day_for_time(event->measured_at, day)) {
        return -EINVAL;
    }
    snprintf(path, sizeof(path), SYNC_HEALTH_ROOT "/%s.conf", day);
    snprintf(temporary, sizeof(temporary),
             SYNC_HEALTH_ROOT "/%s.tmp", day);
    if (event->type == SYNC_HEALTH_HEART_RATE) {
        snprintf(line, sizeof(line), "H,%lld,%u\n",
                 (long long)event->measured_at,
                 (unsigned int)event->primary);
    } else {
        snprintf(line, sizeof(line), "B,%lld,%u,%u,%u\n",
                 (long long)event->measured_at,
                 (unsigned int)event->primary,
                 (unsigned int)event->secondary,
                 (unsigned int)event->pulse);
    }
    line_length = strlen(line);
    if (stat(path, &information) == 0) {
        if (information.st_size < 0 ||
            (size_t)information.st_size > SYNC_MAX_HEALTH_FILE_SIZE) {
            return -EFBIG;
        }
        existing = sync_read_file(path, SYNC_MAX_HEALTH_FILE_SIZE,
                                  &existing_length);
        if (!existing) {
            return -EIO;
        }
    } else if (errno != ENOENT) {
        return -errno;
    }
    if (existing_length + line_length > SYNC_MAX_HEALTH_FILE_SIZE) {
        free(existing);
        return -ENOSPC;
    }
    combined = malloc(existing_length + line_length + 1);
    if (!combined) {
        free(existing);
        return -ENOMEM;
    }
    if (existing_length) {
        memcpy(combined, existing, existing_length);
    }
    memcpy(combined + existing_length, line, line_length + 1);
    result = sync_write_atomic(path, temporary, combined,
                               existing_length + line_length);
    free(combined);
    free(existing);
    return result;
}

static bool sync_parse_unsigned_list(const char *text,
                                     uint32_t *values, size_t count)
{
    const char *cursor = text;
    char *end;
    size_t index;

    for (index = 0; index < count; index++) {
        unsigned long value;

        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        errno = 0;
        value = strtoul(cursor, &end, 10);
        if (end == cursor || errno == ERANGE || value > UINT32_MAX) {
            return false;
        }
        values[index] = (uint32_t)value;
        cursor = end;
        if (index + 1 < count) {
            if (*cursor != ',') {
                return false;
            }
            cursor++;
        }
    }
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
           *cursor == '\n') {
        cursor++;
    }
    return *cursor == '\0';
}

static struct sync_sport_day_s *sync_sport_history_ensure(
    struct sync_sport_file_s *sport, const char *day)
{
    size_t index;

    for (index = 0; index < sport->history_count; index++) {
        if (strcmp(sport->history[index].day, day) == 0) {
            return &sport->history[index];
        }
    }
    if (sport->history_count >= SYNC_MAX_SPORT_HISTORY) {
        return NULL;
    }
    memset(&sport->history[sport->history_count], 0,
           sizeof(sport->history[sport->history_count]));
    sport->history[sport->history_count].valid = true;
    sync_copy(sport->history[sport->history_count].day,
              sizeof(sport->history[sport->history_count].day), day);
    return &sport->history[sport->history_count++];
}

static void sync_sport_normalize(struct sync_sport_day_s *day)
{
    uint32_t previous = 0;
    size_t index;

    if (!day->hours_valid) {
        for (index = 0; index < SYNC_HOURLY_SAMPLES; index++) {
            day->hours[index] = (uint32_t)(
                ((uint64_t)day->steps * (index + 1)) /
                SYNC_HOURLY_SAMPLES);
        }
        day->hours_valid = true;
    }
    for (index = 0; index < SYNC_HOURLY_SAMPLES; index++) {
        if (day->hours[index] < previous) {
            day->hours[index] = previous;
        }
        if (day->hours[index] > day->steps) {
            day->hours[index] = day->steps;
        }
        previous = day->hours[index];
    }
    day->hours[SYNC_HOURLY_SAMPLES - 1] = day->steps;
}

static void sync_sport_load(struct sync_sport_file_s *sport)
{
    FILE *file;
    char line[640];
    char current_day[SYNC_DAY_SIZE] = "";
    uint32_t current_steps = 0;
    uint32_t current_goals[3] = { 8000, 400, 30 };

    memset(sport, 0, sizeof(*sport));
    file = fopen(SYNC_SPORT_STATE_PATH, "r");
    if (!file) {
        return;
    }
    while (fgets(line, sizeof(line), file)) {
        char day[SYNC_DAY_SIZE];
        unsigned long values[4];
        uint32_t hours[SYNC_HOURLY_SAMPLES];

        if (sscanf(line, "data_day=%8s", day) == 1 &&
            sync_day_valid(day)) {
            sync_copy(current_day, sizeof(current_day), day);
        } else if (sscanf(line, "steps=%lu", &values[0]) == 1 &&
                   values[0] <= UINT32_MAX) {
            current_steps = (uint32_t)values[0];
        } else if (sscanf(line, "step_goal=%lu", &values[0]) == 1 &&
                   values[0] <= UINT32_MAX) {
            current_goals[0] = (uint32_t)values[0];
        } else if (sscanf(line, "calorie_goal=%lu", &values[0]) == 1 &&
                   values[0] <= UINT32_MAX) {
            current_goals[1] = (uint32_t)values[0];
        } else if (sscanf(line, "duration_goal=%lu", &values[0]) == 1 &&
                   values[0] <= UINT32_MAX) {
            current_goals[2] = (uint32_t)values[0];
        } else if (strncmp(line, "day_hours=", 10) == 0 &&
                   sync_parse_unsigned_list(line + 10, hours,
                                            SYNC_HOURLY_SAMPLES)) {
            memcpy(sport->current.hours, hours,
                   sizeof(sport->current.hours));
            sport->current.hours_valid = true;
        } else if (sscanf(line, "history=%8[^,],%lu,%lu,%lu,%lu",
                          day, &values[0], &values[1], &values[2],
                          &values[3]) == 5 && sync_day_valid(day) &&
                   values[0] <= UINT32_MAX && values[1] <= UINT32_MAX &&
                   values[2] <= UINT32_MAX && values[3] <= UINT32_MAX) {
            struct sync_sport_day_s *record =
                sync_sport_history_ensure(sport, day);

            if (record) {
                record->steps = (uint32_t)values[0];
                record->goals[0] = (uint32_t)values[1];
                record->goals[1] = (uint32_t)values[2];
                record->goals[2] = (uint32_t)values[3];
            }
        } else if (strncmp(line, "history_hours=", 14) == 0) {
            char *payload = line + 14;
            char *comma = strchr(payload, ',');

            if (comma && comma - payload == 8) {
                struct sync_sport_day_s *record;

                memcpy(day, payload, 8);
                day[8] = '\0';
                record = sync_day_valid(day) ?
                    sync_sport_history_ensure(sport, day) : NULL;
                if (record && sync_parse_unsigned_list(
                        comma + 1, hours, SYNC_HOURLY_SAMPLES)) {
                    memcpy(record->hours, hours, sizeof(record->hours));
                    record->hours_valid = true;
                }
            }
        }
    }
    fclose(file);

    if (sync_day_valid(current_day)) {
        sport->current.valid = true;
        sync_copy(sport->current.day, sizeof(sport->current.day),
                  current_day);
        sport->current.steps = current_steps;
        memcpy(sport->current.goals, current_goals,
               sizeof(sport->current.goals));
        sync_sport_normalize(&sport->current);
    }
    {
        size_t index;

        for (index = 0; index < sport->history_count; index++) {
            sync_sport_normalize(&sport->history[index]);
        }
    }
}

static const struct sync_sport_day_s *sync_sport_find(
    const struct sync_sport_file_s *sport, const char *day)
{
    size_t index;

    if (sport->current.valid && strcmp(sport->current.day, day) == 0) {
        return &sport->current;
    }
    for (index = 0; index < sport->history_count; index++) {
        if (sport->history[index].valid &&
            strcmp(sport->history[index].day, day) == 0) {
            return &sport->history[index];
        }
    }
    return NULL;
}

static void sync_add_health(cJSON *health, const char *day)
{
    cJSON *heart = cJSON_AddArrayToObject(health, "heartRate");
    cJSON *pressure = cJSON_AddArrayToObject(health, "bloodPressure");
    char path[SYNC_PATH_SIZE];
    FILE *file;
    char line[160];
    size_t heart_count = 0;
    size_t pressure_count = 0;

    if (!heart || !pressure) {
        return;
    }
    snprintf(path, sizeof(path), SYNC_HEALTH_ROOT "/%s.conf", day);
    file = fopen(path, "r");
    if (!file) {
        return;
    }
    while (fgets(line, sizeof(line), file)) {
        long long measured;
        unsigned int first;
        unsigned int second;
        unsigned int pulse;

        if (heart_count < SYNC_MAX_HEALTH_RECORDS &&
            sscanf(line, "H,%lld,%u", &measured, &first) == 2 &&
            first >= 30 && first <= 220) {
            cJSON *record = cJSON_CreateObject();

            cJSON_AddNumberToObject(record, "bpm", first);
            cJSON_AddNumberToObject(record, "measuredAt",
                                    (double)measured * 1000.0);
            cJSON_AddStringToObject(record, "source", "simulated");
            cJSON_AddItemToArray(heart, record);
            heart_count++;
        } else if (pressure_count < SYNC_MAX_HEALTH_RECORDS &&
                   sscanf(line, "B,%lld,%u,%u,%u", &measured, &first,
                          &second, &pulse) == 4 && first >= 70 &&
                   first <= 220 && second >= 40 && second <= 140 &&
                   pulse >= 30 && pulse <= 220) {
            cJSON *record = cJSON_CreateObject();

            cJSON_AddNumberToObject(record, "systolic", first);
            cJSON_AddNumberToObject(record, "diastolic", second);
            cJSON_AddNumberToObject(record, "pulse", pulse);
            cJSON_AddNumberToObject(record, "measuredAt",
                                    (double)measured * 1000.0);
            cJSON_AddStringToObject(record, "source", "simulated");
            cJSON_AddItemToArray(pressure, record);
            pressure_count++;
        }
    }
    fclose(file);
}

static bool sync_document_complete(const cJSON *root,
                                   uint64_t simulation_id)
{
    const cJSON *sport = cJSON_GetObjectItemCaseSensitive(root, "sport");
    const cJSON *health = cJSON_GetObjectItemCaseSensitive(root, "health");
    const cJSON *hours = sport ? cJSON_GetObjectItemCaseSensitive(
        sport, "hourlyCumulativeSteps") : NULL;
    const cJSON *goals = sport ?
        cJSON_GetObjectItemCaseSensitive(sport, "goals") : NULL;
    const cJSON *heart = health ?
        cJSON_GetObjectItemCaseSensitive(health, "heartRate") : NULL;
    const cJSON *pressure = health ?
        cJSON_GetObjectItemCaseSensitive(health, "bloodPressure") : NULL;
    const cJSON *simulation = cJSON_GetObjectItemCaseSensitive(
        root, "simulation");

    return cJSON_IsObject(root) && cJSON_IsObject(sport) &&
           cJSON_IsObject(goals) && cJSON_IsObject(health) &&
           cJSON_IsArray(hours) &&
           cJSON_GetArraySize(hours) == SYNC_HOURLY_SAMPLES &&
           cJSON_IsArray(heart) && cJSON_IsArray(pressure) &&
           ((simulation_id != 0 && cJSON_IsObject(simulation)) ||
            (simulation_id == 0 && simulation == NULL));
}

static char *sync_create_document(const struct sync_config_s *config,
                                  const char *source_day,
                                  uint64_t simulation_id)
{
    struct sync_sport_file_s sport_file;
    const struct sync_sport_day_s *record;
    struct sync_sport_day_s empty;
    char dashed[SYNC_DASHED_DAY_SIZE];
    char date[64];
    char sync_id[192];
    cJSON *root;
    cJSON *sport;
    cJSON *hours;
    cJSON *goals;
    cJSON *health;
    char *document;
    size_t index;

    sync_sport_load(&sport_file);
    record = sync_sport_find(&sport_file, source_day);
    if (!record) {
        memset(&empty, 0, sizeof(empty));
        empty.valid = true;
        empty.hours_valid = true;
        sync_copy(empty.day, sizeof(empty.day), source_day);
        empty.goals[0] = 8000;
        empty.goals[1] = 400;
        empty.goals[2] = 30;
        record = &empty;
    }

    sync_day_dashed(source_day, dashed);
    if (simulation_id != 0) {
        snprintf(date, sizeof(date), "simulation-%llu",
                 (unsigned long long)simulation_id);
    } else {
        sync_copy(date, sizeof(date), dashed);
    }
    snprintf(sync_id, sizeof(sync_id), "%s:%s:v1",
             config->device_id, date);

    root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    cJSON_AddNumberToObject(root, "schemaVersion", 1);
    cJSON_AddStringToObject(root, "messageType", SYNC_MESSAGE_TYPE);
    cJSON_AddStringToObject(root, "syncId", sync_id);
    cJSON_AddStringToObject(root, "deviceId", config->device_id);
    cJSON_AddStringToObject(root, "date", date);
    cJSON_AddNumberToObject(root, "timezoneOffsetMinutes", 480);
    cJSON_AddNumberToObject(root, "generatedAt",
                            (double)time(NULL) * 1000.0);

    sport = cJSON_AddObjectToObject(root, "sport");
    if (!sport) {
        goto document_failed;
    }
    cJSON_AddNumberToObject(sport, "steps", record->steps);
    cJSON_AddNumberToObject(sport, "caloriesKcal",
                            ((uint64_t)record->steps * 4U + 50U) / 100U);
    cJSON_AddNumberToObject(sport, "activeMinutes", record->steps / 100U);
    hours = cJSON_AddArrayToObject(sport, "hourlyCumulativeSteps");
    if (!hours) {
        goto document_failed;
    }
    for (index = 0; index < SYNC_HOURLY_SAMPLES; index++) {
        cJSON_AddItemToArray(hours, cJSON_CreateNumber(record->hours[index]));
    }
    goals = cJSON_AddObjectToObject(sport, "goals");
    if (!goals) {
        goto document_failed;
    }
    cJSON_AddNumberToObject(goals, "steps", record->goals[0]);
    cJSON_AddNumberToObject(goals, "caloriesKcal", record->goals[1]);
    cJSON_AddNumberToObject(goals, "activeMinutes", record->goals[2]);
    cJSON_AddStringToObject(sport, "source", "simulated_or_derived");

    health = cJSON_AddObjectToObject(root, "health");
    if (!health) {
        goto document_failed;
    }
    sync_add_health(health, source_day);
    if (simulation_id != 0) {
        cJSON *simulation = cJSON_AddObjectToObject(root, "simulation");

        if (!simulation) {
            goto document_failed;
        }
        cJSON_AddBoolToObject(simulation, "enabled", true);
        cJSON_AddNumberToObject(simulation, "requestId",
                                (double)simulation_id);
        cJSON_AddStringToObject(simulation, "sourceDate", dashed);
    }

    if (!sync_document_complete(root, simulation_id)) {
        goto document_failed;
    }

    document = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (document && strlen(document) > SYNC_MAX_DOCUMENT_SIZE) {
        free(document);
        return NULL;
    }
    return document;

document_failed:
    cJSON_Delete(root);
    return NULL;
}

static void sync_outbox_path(char *path, size_t size, const char *day,
                             uint64_t simulation_id)
{
    if (simulation_id != 0) {
        snprintf(path, size, SYNC_OUTBOX_ROOT "/%s-%llu.json", day,
                 (unsigned long long)simulation_id);
    } else {
        snprintf(path, size, SYNC_OUTBOX_ROOT "/%s.json", day);
    }
}

static void sync_outbox_temporary_path(char *path, size_t size,
                                       const char *day,
                                       uint64_t simulation_id)
{
    if (simulation_id != 0) {
        snprintf(path, size, SYNC_OUTBOX_ROOT "/%s-%llu.tmp", day,
                 (unsigned long long)simulation_id);
    } else {
        snprintf(path, size, SYNC_OUTBOX_ROOT "/%s.tmp", day);
    }
}

static bool sync_outbox_exists(const char *day, uint64_t simulation_id)
{
    char path[SYNC_PATH_SIZE];

    sync_outbox_path(path, sizeof(path), day, simulation_id);
    return access(path, F_OK) == 0;
}

static void sync_prune_simulation_outbox(void)
{
    for (;;) {
        DIR *directory = opendir(SYNC_OUTBOX_ROOT);
        struct dirent *entry;
        char oldest[128] = "";
        size_t count = 0;

        if (!directory) {
            return;
        }
        while ((entry = readdir(directory)) != NULL) {
            size_t length = strlen(entry->d_name);

            if (length <= 14 || entry->d_name[8] != '-' ||
                strcmp(entry->d_name + length - 5, ".json") != 0) {
                continue;
            }
            count++;
            if (!oldest[0] || strcmp(entry->d_name, oldest) < 0) {
                sync_copy(oldest, sizeof(oldest), entry->d_name);
            }
        }
        closedir(directory);
        if (count < SYNC_MAX_SIMULATION_OUTBOX || !oldest[0]) {
            return;
        }
        {
            char path[SYNC_PATH_SIZE];

            snprintf(path, sizeof(path), SYNC_OUTBOX_ROOT "/%s", oldest);
            if (unlink(path) != 0) {
                return;
            }
        }
    }
}

static int sync_enqueue_document(const struct sync_config_s *config,
                                 const char *day,
                                 uint64_t simulation_id)
{
    char path[SYNC_PATH_SIZE];
    char temporary[SYNC_PATH_SIZE];
    char *document;
    int result;

    if (!sync_day_valid(day) || sync_outbox_exists(day, simulation_id)) {
        return 0;
    }
    if (simulation_id != 0) {
        sync_prune_simulation_outbox();
    }
    document = sync_create_document(config, day, simulation_id);
    if (!document) {
        return -ENOMEM;
    }
    sync_outbox_path(path, sizeof(path), day, simulation_id);
    sync_outbox_temporary_path(temporary, sizeof(temporary), day,
                               simulation_id);
    result = sync_write_atomic(path, temporary, document, strlen(document));
    if (result == 0) {
        syslog(LOG_INFO,
               "openvela_ui: sync queued %s%s\n", day,
               simulation_id ? " (simulation)" : "");
    }
    free(document);
    return result;
}

static size_t sync_http_write(char *data, size_t size, size_t count,
                              void *user_data)
{
    struct sync_http_response_s *response = user_data;
    size_t bytes = size * count;
    size_t available;

    if (response->length >= sizeof(response->data) - 1) {
        return 0;
    }
    available = sizeof(response->data) - 1 - response->length;
    if (bytes > available) {
        bytes = available;
    }
    memcpy(response->data + response->length, data, bytes);
    response->length += bytes;
    response->data[response->length] = '\0';
    return bytes == size * count ? bytes : 0;
}

static int sync_http_request(CURL *curl,
                             const struct sync_config_s *config,
                             const char *url, const char *body,
                             struct sync_http_response_s *response)
{
    CURLcode result;
    struct curl_slist *headers = NULL;
    char authorization[SYNC_TOKEN_SIZE + 32];
    char error_buffer[CURL_ERROR_SIZE] = "";
    long response_code = 0;
    long os_error = 0;

    memset(response, 0, sizeof(*response));
    if (!curl) {
        return -EINVAL;
    }
    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 12L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "openvela-ui-sync/1.0");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, sync_http_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    if (strncmp(url, "https://", 8) == 0) {
        const char *ca = access(SYNC_CA_RESOURCE, R_OK) == 0 ?
                         SYNC_CA_RESOURCE : SYNC_CA_PRIMARY;

        curl_easy_setopt(curl, CURLOPT_CAINFO, ca);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    }
    if (config->token[0]) {
        snprintf(authorization, sizeof(authorization),
                 "Authorization: Bearer %s", config->token);
        headers = curl_slist_append(headers, authorization);
    }
    if (body) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    } else {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    }
    if (headers) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    }

    result = curl_easy_perform(curl);
    if (result == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    } else {
        curl_easy_getinfo(curl, CURLINFO_OS_ERRNO, &os_error);
    }
    curl_slist_free_all(headers);
    if (result != CURLE_OK) {
        syslog(LOG_WARNING,
               "openvela_ui: sync curl failed: curl=%d (%s), os=%ld%s%s\n",
               (int)result, curl_easy_strerror(result), os_error,
               error_buffer[0] ? ", detail=" : "",
               error_buffer[0] ? error_buffer : "");
        return -EIO;
    }
    if (response_code < 200 || response_code >= 300) {
        return response_code > 0 ? -(int)response_code : -EPROTO;
    }
    return 0;
}

static int sync_curl_ensure(CURL **curl)
{
    if (*curl) {
        return 0;
    }

    *curl = curl_easy_init();
    if (!*curl) {
        syslog(LOG_ERR, "openvela_ui: curl handle allocation failed\n");
        return -ENOMEM;
    }
    return 0;
}

static void sync_curl_discard(CURL **curl)
{
    if (*curl) {
        curl_easy_cleanup(*curl);
        *curl = NULL;
    }
}

static uint32_t sync_checksum(const char *text, size_t length)
{
    uint32_t hash = 2166136261U;
    size_t index;

    for (index = 0; index < length; index++) {
        hash ^= (uint8_t)text[index];
        hash *= 16777619U;
    }
    return hash;
}

static int sync_post_frame(CURL *curl,
                           const struct sync_config_s *config,
                           cJSON *frame,
                           struct sync_http_response_s *response)
{
    char *body = cJSON_PrintUnformatted(frame);
    int result;

    if (!body) {
        return -ENOMEM;
    }
    result = sync_http_request(curl, config, config->endpoint, body,
                               response);
    free(body);
    return result;
}

static int sync_send_document(CURL *curl,
                              const struct sync_config_s *config,
                              const char *document)
{
    cJSON *payload;
    const cJSON *sync_id_item;
    const cJSON *date_item;
    const char *sync_id;
    const char *date;
    size_t length = strlen(document);
    size_t total_chunks = (length + SYNC_CHUNK_SIZE - 1) / SYNC_CHUNK_SIZE;
    char checksum[9];
    cJSON *frame;
    struct sync_http_response_s response;
    size_t index;
    int result;

    payload = cJSON_Parse(document);
    if (!payload) {
        return -EINVAL;
    }
    sync_id_item = cJSON_GetObjectItemCaseSensitive(payload, "syncId");
    date_item = cJSON_GetObjectItemCaseSensitive(payload, "date");
    if (!cJSON_IsString(sync_id_item) || !cJSON_IsString(date_item) ||
        !sync_id_item->valuestring || !date_item->valuestring) {
        cJSON_Delete(payload);
        return -EINVAL;
    }
    sync_id = sync_id_item->valuestring;
    date = date_item->valuestring;
    snprintf(checksum, sizeof(checksum), "%08lx",
             (unsigned long)sync_checksum(document, length));

    frame = cJSON_CreateObject();
    cJSON_AddStringToObject(frame, "protocol", SYNC_PROTOCOL);
    cJSON_AddNumberToObject(frame, "version", SYNC_VERSION);
    cJSON_AddStringToObject(frame, "type", "sync_begin");
    cJSON_AddStringToObject(frame, "syncId", sync_id);
    cJSON_AddStringToObject(frame, "date", date);
    cJSON_AddNumberToObject(frame, "totalChunks", total_chunks);
    cJSON_AddNumberToObject(frame, "totalCharacters", length);
    cJSON_AddStringToObject(frame, "checksum", checksum);
    result = sync_post_frame(curl, config, frame, &response);
    cJSON_Delete(frame);
    if (result < 0) {
        cJSON_Delete(payload);
        return result;
    }

    for (index = 0; index < total_chunks; index++) {
        size_t offset = index * SYNC_CHUNK_SIZE;
        size_t chunk_length = length - offset;
        char chunk[SYNC_CHUNK_SIZE + 1];

        if (chunk_length > SYNC_CHUNK_SIZE) {
            chunk_length = SYNC_CHUNK_SIZE;
        }
        memcpy(chunk, document + offset, chunk_length);
        chunk[chunk_length] = '\0';
        frame = cJSON_CreateObject();
        cJSON_AddStringToObject(frame, "protocol", SYNC_PROTOCOL);
        cJSON_AddNumberToObject(frame, "version", SYNC_VERSION);
        cJSON_AddStringToObject(frame, "type", "sync_chunk");
        cJSON_AddStringToObject(frame, "syncId", sync_id);
        cJSON_AddNumberToObject(frame, "index", index);
        cJSON_AddStringToObject(frame, "data", chunk);
        result = sync_post_frame(curl, config, frame, &response);
        cJSON_Delete(frame);
        if (result < 0) {
            cJSON_Delete(payload);
            return result;
        }
    }

    frame = cJSON_CreateObject();
    cJSON_AddStringToObject(frame, "protocol", SYNC_PROTOCOL);
    cJSON_AddNumberToObject(frame, "version", SYNC_VERSION);
    cJSON_AddStringToObject(frame, "type", "sync_commit");
    cJSON_AddStringToObject(frame, "syncId", sync_id);
    result = sync_post_frame(curl, config, frame, &response);
    cJSON_Delete(frame);
    if (result == 0) {
        cJSON *reply = cJSON_Parse(response.data);
        cJSON *nested_ack = reply ?
            cJSON_GetObjectItemCaseSensitive(reply, "ack") : NULL;
        cJSON *ack = cJSON_IsObject(nested_ack) ? nested_ack : reply;
        cJSON *protocol = ack ?
            cJSON_GetObjectItemCaseSensitive(ack, "protocol") : NULL;
        cJSON *version = ack ?
            cJSON_GetObjectItemCaseSensitive(ack, "version") : NULL;
        cJSON *type = ack ?
            cJSON_GetObjectItemCaseSensitive(ack, "type") : NULL;
        cJSON *ack_id = ack ?
            cJSON_GetObjectItemCaseSensitive(ack, "syncId") : NULL;
        cJSON *status = ack ?
            cJSON_GetObjectItemCaseSensitive(ack, "status") : NULL;

        if (!cJSON_IsString(protocol) ||
            strcmp(protocol->valuestring, SYNC_PROTOCOL) != 0 ||
            !cJSON_IsNumber(version) ||
            version->valueint != SYNC_VERSION ||
            !cJSON_IsString(type) ||
            strcmp(type->valuestring, "sync_ack") != 0 ||
            !cJSON_IsString(ack_id) || !cJSON_IsString(status) ||
            strcmp(ack_id->valuestring, sync_id) != 0 ||
            (strcmp(status->valuestring, "ok") != 0 &&
             strcmp(status->valuestring, "duplicate") != 0)) {
            result = -EPROTO;
        }
        cJSON_Delete(reply);
    }
    cJSON_Delete(payload);
    return result;
}

static bool sync_find_outbox(char path[SYNC_PATH_SIZE])
{
    DIR *directory;
    struct dirent *entry;
    char selected[128] = "";

    directory = opendir(SYNC_OUTBOX_ROOT);
    if (!directory) {
        return false;
    }
    while ((entry = readdir(directory)) != NULL) {
        size_t length = strlen(entry->d_name);

        if (length <= 5 || strcmp(entry->d_name + length - 5, ".json") != 0) {
            continue;
        }
        if (!selected[0] || strcmp(entry->d_name, selected) < 0) {
            sync_copy(selected, sizeof(selected), entry->d_name);
        }
    }
    closedir(directory);
    if (!selected[0]) {
        return false;
    }
    snprintf(path, SYNC_PATH_SIZE, SYNC_OUTBOX_ROOT "/%s", selected);
    return true;
}

static int sync_upload_one(CURL *curl,
                           const struct sync_config_s *config,
                           struct sync_state_s *state)
{
    char path[SYNC_PATH_SIZE];
    size_t length;
    char *document;
    cJSON *payload;
    cJSON *date_item;
    cJSON *simulation;
    char formal_day[SYNC_DAY_SIZE] = "";
    int result;

    if (!sync_find_outbox(path)) {
        return 1;
    }
    document = sync_read_file(path, SYNC_MAX_DOCUMENT_SIZE, &length);
    if (!document) {
        return -EIO;
    }
    result = sync_send_document(curl, config, document);
    if (result < 0) {
        free(document);
        return result;
    }

    payload = cJSON_Parse(document);
    date_item = payload ?
        cJSON_GetObjectItemCaseSensitive(payload, "date") : NULL;
    simulation = payload ?
        cJSON_GetObjectItemCaseSensitive(payload, "simulation") : NULL;
    if (!simulation && cJSON_IsString(date_item) &&
        date_item->valuestring && strlen(date_item->valuestring) == 10) {
        snprintf(formal_day, sizeof(formal_day), "%.4s%.2s%.2s",
                 date_item->valuestring, date_item->valuestring + 5,
                 date_item->valuestring + 8);
    }
    if (sync_day_valid(formal_day)) {
        sync_state_add_day(state, formal_day);
        if (sync_state_save(state) < 0) {
            cJSON_Delete(payload);
            free(document);
            return -EIO;
        }
    }
    cJSON_Delete(payload);
    free(document);
    if (unlink(path) != 0) {
        return -errno;
    }
    syslog(LOG_INFO, "openvela_ui: sync upload acknowledged\n");
    return 0;
}

static void sync_audit_days(const struct sync_config_s *config,
                            struct sync_state_s *state)
{
    char today[SYNC_DAY_SIZE];
    char cutoff[SYNC_DAY_SIZE];
    char cursor[SYNC_DAY_SIZE];
    int count;
    bool state_changed = false;

    if (!sync_today(today)) {
        return;
    }
    if (!sync_day_valid(state->activated_day)) {
        sync_copy(state->activated_day, sizeof(state->activated_day), today);
        sync_state_save(state);
        return;
    }
    if (!sync_day_before(SYNC_MAX_SYNCED_DAYS - 1, cutoff)) {
        return;
    }
    if (strcmp(state->activated_day, cutoff) < 0) {
        sync_copy(state->activated_day, sizeof(state->activated_day), cutoff);
        state_changed = true;
    }
    sync_state_prune(state, cutoff);
    sync_copy(cursor, sizeof(cursor), state->activated_day);
    for (count = 0; count < SYNC_MAX_SYNCED_DAYS &&
                    strcmp(cursor, today) < 0; count++) {
        char next[SYNC_DAY_SIZE];

        if (!sync_state_has_day(state, cursor) &&
            !sync_outbox_exists(cursor, 0)) {
            sync_enqueue_document(config, cursor, 0);
        }
        if (!sync_next_day(cursor, next)) {
            break;
        }
        sync_copy(cursor, sizeof(cursor), next);
    }
    if (state_changed) {
        sync_state_save(state);
    }
}

static int sync_poll_control(CURL *curl,
                             const struct sync_config_s *config,
                             struct sync_state_s *state,
                             bool *transport_error)
{
    struct sync_http_response_s response;
    cJSON *root;
    cJSON *request_id;
    double request_value;
    uint64_t identifier;
    uint64_t previous_identifier;
    char today[SYNC_DAY_SIZE];
    int result;

    *transport_error = false;
    if (!config->control_url[0] || !sync_today(today)) {
        return 0;
    }
    result = sync_http_request(curl, config, config->control_url, NULL,
                               &response);
    if (result < 0) {
        *transport_error = result == -EIO;
        return result;
    }
    root = cJSON_Parse(response.data);
    request_id = root ?
        cJSON_GetObjectItemCaseSensitive(root, "requestId") : NULL;
    request_value = cJSON_IsNumber(request_id) ?
        request_id->valuedouble : -1.0;
    if (!cJSON_IsNumber(request_id) || request_value != request_value ||
        request_value < 0 || request_value > SYNC_MAX_SAFE_REQUEST_ID) {
        cJSON_Delete(root);
        return -EPROTO;
    }
    if (request_value == 0) {
        cJSON_Delete(root);
        return 0;
    }
    identifier = (uint64_t)request_value;
    cJSON_Delete(root);
    if ((double)identifier != request_value) {
        return -EPROTO;
    }
    if (identifier <= state->last_control_id) {
        return 0;
    }
    result = sync_enqueue_document(config, today, identifier);
    if (result < 0) {
        return result;
    }
    previous_identifier = state->last_control_id;
    state->last_control_id = identifier;
    result = sync_state_save(state);
    if (result < 0) {
        state->last_control_id = previous_identifier;
    }
    return result;
}

static size_t sync_drain_health(struct sync_health_event_s *events,
                                size_t capacity)
{
    size_t count = 0;

    pthread_mutex_lock(&g_sync.lock);
    while (count < capacity && g_sync.queue_count > 0) {
        events[count++] = g_sync.queue[g_sync.queue_head];
        g_sync.queue_head = (g_sync.queue_head + 1) %
                            SYNC_HEALTH_QUEUE_SIZE;
        g_sync.queue_count--;
    }
    pthread_mutex_unlock(&g_sync.lock);
    return count;
}

static bool sync_is_stopping(void)
{
    bool stopping;

    pthread_mutex_lock(&g_sync.lock);
    stopping = g_sync.stopping;
    pthread_mutex_unlock(&g_sync.lock);
    return stopping;
}

static void sync_worker_wait(bool wake_for_health)
{
    struct timespec deadline;

    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += SYNC_WORKER_INTERVAL_SECONDS;
    pthread_mutex_lock(&g_sync.lock);
    if (!g_sync.stopping &&
        (!wake_for_health || g_sync.queue_count == 0)) {
        pthread_cond_timedwait(&g_sync.condition, &g_sync.lock, &deadline);
    }
    pthread_mutex_unlock(&g_sync.lock);
}

static void *sync_worker(void *argument)
{
    struct sync_config_s config;
    struct sync_state_s state;
    struct sync_health_event_s events[SYNC_HEALTH_QUEUE_SIZE];
    CURL *curl = NULL;
    time_t next_control = 0;
    time_t next_upload = 0;
    time_t last_control_error_log = 0;
    unsigned int control_retry_seconds = SYNC_CONTROL_INTERVAL_SECONDS;
    unsigned int retry_seconds = SYNC_RETRY_INITIAL_SECONDS;
    int last_control_error = 0;

    (void)argument;
    sync_load_config(&config);
    if (!config.enabled) {
        syslog(LOG_INFO, "openvela_ui: Wi-Fi data sync disabled\n");
        return NULL;
    }
    sync_ensure_directories();
    sync_state_load(&state);
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        syslog(LOG_ERR, "openvela_ui: curl initialization failed\n");
        return NULL;
    }
    syslog(LOG_INFO, "openvela_ui: Wi-Fi data sync started for %s\n",
           config.device_id);

    while (!sync_is_stopping()) {
        char valid_today[SYNC_DAY_SIZE];
        bool defer_upload = false;
        size_t count;
        size_t index;
        time_t now = time(NULL);

        if (!sync_today(valid_today)) {
            sync_worker_wait(false);
            continue;
        }
        count = sync_drain_health(events, SYNC_HEALTH_QUEUE_SIZE);
        for (index = 0; index < count; index++) {
            char measured_day[SYNC_DAY_SIZE];

            if (!sync_day_for_time(events[index].measured_at,
                                   measured_day)) {
                events[index].measured_at = now;
            }
            if (sync_persist_health(&events[index]) < 0) {
                syslog(LOG_ERR,
                       "openvela_ui: failed to persist health sample\n");
            }
        }
        sync_audit_days(&config, &state);
        if (now >= next_control && !sync_resolver_ready(&config)) {
            next_control = now + SYNC_CONTROL_INTERVAL_SECONDS;
            defer_upload = true;
        }
        if (!defer_upload && now >= next_control) {
            bool transport_error = false;
            int result = sync_curl_ensure(&curl);

            if (result == 0) {
                result = sync_poll_control(curl, &config, &state,
                                           &transport_error);
            }

            if (result < 0) {
                unsigned int delay = transport_error ?
                    control_retry_seconds : SYNC_CONTROL_INTERVAL_SECONDS;

                if (result != last_control_error ||
                    last_control_error_log == 0 ||
                    now < last_control_error_log ||
                    now - last_control_error_log >=
                        SYNC_CONTROL_ERROR_LOG_SECONDS) {
                    syslog(LOG_WARNING,
                           "openvela_ui: sync control poll failed: %d, "
                           "retry in %u s\n", result, delay);
                    last_control_error_log = now;
                }
                last_control_error = result;
                next_control = now + delay;
                if (transport_error) {
                    /* c-ares captures /tmp/resolv.conf when an easy handle is
                     * created.  The UI starts in parallel with Wi-Fi, so a
                     * handle created before DHCP must be destroyed rather
                     * than reset; the next retry then loads the live DNS. */
                    sync_curl_discard(&curl);
                    defer_upload = true;
                    control_retry_seconds =
                        control_retry_seconds <
                            SYNC_RETRY_MAX_SECONDS / 2 ?
                        control_retry_seconds * 2 : SYNC_RETRY_MAX_SECONDS;
                } else {
                    control_retry_seconds = SYNC_CONTROL_INTERVAL_SECONDS;
                }
            } else {
                if (last_control_error != 0) {
                    syslog(LOG_INFO,
                           "openvela_ui: sync control poll recovered\n");
                }
                last_control_error = 0;
                last_control_error_log = 0;
                control_retry_seconds = SYNC_CONTROL_INTERVAL_SECONDS;
                next_control = now + SYNC_CONTROL_INTERVAL_SECONDS;
            }
        }
        if (!defer_upload && now >= next_upload) {
            int result = sync_curl_ensure(&curl);

            if (result == 0) {
                result = sync_upload_one(curl, &config, &state);
            }

            if (result == 0) {
                retry_seconds = SYNC_RETRY_INITIAL_SECONDS;
                next_upload = now;
            } else if (result == 1) {
                retry_seconds = SYNC_RETRY_INITIAL_SECONDS;
                next_upload = now + SYNC_WORKER_INTERVAL_SECONDS;
            } else {
                syslog(LOG_WARNING,
                       "openvela_ui: sync upload failed: %d, retry in %u s\n",
                       result, retry_seconds);
                if (result == -EIO) {
                    sync_curl_discard(&curl);
                }
                next_upload = now + retry_seconds;
                retry_seconds = retry_seconds < SYNC_RETRY_MAX_SECONDS / 2 ?
                    retry_seconds * 2 : SYNC_RETRY_MAX_SECONDS;
            }
        }
        sync_worker_wait(true);
    }
    sync_curl_discard(&curl);
    curl_global_cleanup();
    return NULL;
}

int openvela_ui_sync_start(void)
{
    pthread_attr_t attributes;
    int result;

    pthread_mutex_lock(&g_sync.lock);
    if (g_sync.started) {
        pthread_mutex_unlock(&g_sync.lock);
        return 0;
    }
    g_sync.stopping = false;
    g_sync.queue_head = 0;
    g_sync.queue_count = 0;
    g_sync.started = true;
    pthread_mutex_unlock(&g_sync.lock);

    result = pthread_attr_init(&attributes);
    if (result != 0) {
        goto start_failed;
    }
    result = pthread_attr_setstacksize(&attributes,
                                       SYNC_THREAD_STACK_SIZE);
    if (result != 0) {
        pthread_attr_destroy(&attributes);
        goto start_failed;
    }
    result = pthread_create(&g_sync.thread, &attributes, sync_worker, NULL);
    pthread_attr_destroy(&attributes);
    if (result == 0) {
        return 0;
    }

start_failed:
    pthread_mutex_lock(&g_sync.lock);
    g_sync.started = false;
    pthread_mutex_unlock(&g_sync.lock);
    syslog(LOG_ERR,
           "openvela_ui: failed to start Wi-Fi sync worker: %d\n",
           result);
    return -result;
}

void openvela_ui_sync_stop(void)
{
    pthread_t thread;

    pthread_mutex_lock(&g_sync.lock);
    if (!g_sync.started) {
        pthread_mutex_unlock(&g_sync.lock);
        return;
    }
    g_sync.stopping = true;
    thread = g_sync.thread;
    pthread_cond_signal(&g_sync.condition);
    pthread_mutex_unlock(&g_sync.lock);
    pthread_join(thread, NULL);

    pthread_mutex_lock(&g_sync.lock);
    g_sync.started = false;
    g_sync.stopping = false;
    g_sync.queue_head = 0;
    g_sync.queue_count = 0;
    pthread_mutex_unlock(&g_sync.lock);
}

static void sync_queue_health(const struct sync_health_event_s *event)
{
    size_t tail;

    pthread_mutex_lock(&g_sync.lock);
    if (!g_sync.started || g_sync.stopping) {
        pthread_mutex_unlock(&g_sync.lock);
        return;
    }
    if (g_sync.queue_count == SYNC_HEALTH_QUEUE_SIZE) {
        g_sync.queue_head = (g_sync.queue_head + 1) %
                            SYNC_HEALTH_QUEUE_SIZE;
        g_sync.queue_count--;
        syslog(LOG_WARNING,
               "openvela_ui: health sync queue full; dropping oldest sample\n");
    }
    tail = (g_sync.queue_head + g_sync.queue_count) %
           SYNC_HEALTH_QUEUE_SIZE;
    g_sync.queue[tail] = *event;
    g_sync.queue_count++;
    pthread_cond_signal(&g_sync.condition);
    pthread_mutex_unlock(&g_sync.lock);
}

void openvela_ui_sync_record_heart_rate(uint16_t bpm)
{
    struct sync_health_event_s event;

    if (bpm < 30 || bpm > 220) {
        return;
    }
    memset(&event, 0, sizeof(event));
    event.type = SYNC_HEALTH_HEART_RATE;
    event.measured_at = time(NULL);
    event.primary = bpm;
    sync_queue_health(&event);
}

void openvela_ui_sync_record_blood_pressure(uint16_t systolic,
                                             uint16_t diastolic,
                                             uint16_t pulse)
{
    struct sync_health_event_s event;

    if (systolic < 70 || systolic > 220 || diastolic < 40 ||
        diastolic > 140 || pulse < 30 || pulse > 220) {
        return;
    }
    memset(&event, 0, sizeof(event));
    event.type = SYNC_HEALTH_BLOOD_PRESSURE;
    event.measured_at = time(NULL);
    event.primary = systolic;
    event.secondary = diastolic;
    event.pulse = pulse;
    sync_queue_health(&event);
}
