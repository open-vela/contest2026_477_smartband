#include <nuttx/config.h>

#include <lvgl/lvgl.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "openvela_ui_sport.h"

#ifndef CONFIG_OPENVELA_UI_DATA_ROOT
#  define CONFIG_OPENVELA_UI_DATA_ROOT "/data/openvela_ui"
#endif

#define SPORT_DESIGN_WIDTH 432
#define SPORT_DESIGN_HEIGHT 514
#define SPORT_PATH_SIZE 160
#define SPORT_CHART_POINTS 25
#define SPORT_HOURLY_SAMPLES 24
#define SPORT_LEGACY_SAMPLES 12
#define SPORT_CHART_VIEW_WIDTH 338
#define SPORT_CHART_CONTENT_WIDTH 1125
#define SPORT_CHART_HOUR_WIDTH 45
#define SPORT_CHART_POINT_X 23
#define SPORT_CHART_BASELINE_Y 127
#define SPORT_CHART_RANGE_Y 112
#define SPORT_BUBBLE_HIDE_MS 2600
#define SPORT_CHART_GESTURE_GUARD_MS 160
#define SPORT_GESTURE_THRESHOLD 45
#define SPORT_ENTER_FEEDBACK_MS 260
#define SPORT_STEP_PERIOD_MS 2600
#define SPORT_HEART_PERIOD_MS 1800
#define SPORT_MOTION_PERIOD_MS 500
#define SPORT_HISTORY_COUNT 7
#define SPORT_HISTORY_STORED_COUNT (SPORT_HISTORY_COUNT - 1)
#define SPORT_METRIC_COUNT 3
#define SPORT_MAX_STEPS 100000000U
#define SPORT_CLOCK_VALID_YEAR 2020
#define SPORT_STATE_PATH "/data/etc/openvela_ui/sport_state.conf"
#define SPORT_STATE_TMP_PATH "/data/etc/openvela_ui/sport_state.tmp"

#define SPORT_BACKGROUND_PATH \
    CONFIG_OPENVELA_UI_DATA_ROOT "/backgrounds/dark-purple.png"
#define SPORT_JUMP_ROPE_PATH \
    CONFIG_OPENVELA_UI_DATA_ROOT "/sport/jump-rope.png"
#define SPORT_HEART_PATH \
    CONFIG_OPENVELA_UI_DATA_ROOT "/sport/heart.png"

typedef enum {
    SPORT_STATS_TODAY = 0,
    SPORT_STATS_HISTORY,
    SPORT_STATS_HISTORY_DETAIL,
    SPORT_STATS_GOAL,
    SPORT_STATS_GOAL_EDITOR,
} sport_stats_view_t;

typedef struct {
    bool valid;
    bool samples_valid;
    char day[9];
    uint32_t steps;
    uint32_t goals[SPORT_METRIC_COUNT];
    /* End-of-hour cumulative values for hours 00:00-01:00 through
     * 23:00-24:00.  Rendering prepends the 00:00 zero boundary. */
    uint32_t samples[SPORT_HOURLY_SAMPLES];
} sport_history_record_t;

struct openvela_ui_sport_s {
    lv_obj_t *root;
    lv_obj_t *background;
    lv_obj_t *entry_page;
    lv_obj_t *stats_page;
    lv_obj_t *history_page;
    lv_obj_t *goal_page;
    lv_obj_t *goal_editor_page;
    lv_obj_t *heart_page;
    lv_obj_t *exit_page;
    lv_obj_t *confirm_layer;
    lv_obj_t *celebration_layer;

    lv_obj_t *entry_cat_visual;
    lv_obj_t *entry_cat_image;
    lv_obj_t *exit_cat_visual;
    lv_obj_t *exit_cat_image;
    lv_obj_t *heart_visual;
    lv_obj_t *heart_image;
    lv_obj_t *enter_button;
    lv_obj_t *enter_button_label;

    lv_obj_t *stats_title;
    lv_obj_t *stats_current_label;
    lv_obj_t *stats_value;
    lv_obj_t *stats_unit;
    lv_obj_t *chart_heading;
    lv_obj_t *chart_goal_text;
    lv_obj_t *chart_scroll;
    lv_obj_t *chart_content;
    lv_obj_t *chart_goal_line;
    lv_obj_t *chart_line;
    lv_obj_t *chart_dots[SPORT_CHART_POINTS];
    lv_obj_t *chart_hour_labels[SPORT_CHART_POINTS];
    lv_obj_t *chart_hour_targets[SPORT_CHART_POINTS];
    lv_obj_t *chart_bubble;
    lv_obj_t *chart_bubble_label;
    lv_point_precise_t chart_points[SPORT_CHART_POINTS];
    uint32_t chart_hour_values[SPORT_CHART_POINTS];
    uint32_t chart_maximum;

    lv_obj_t *history_title;
    lv_obj_t *history_rows[SPORT_HISTORY_COUNT];
    lv_obj_t *history_dots[SPORT_HISTORY_COUNT];
    lv_obj_t *history_date_labels[SPORT_HISTORY_COUNT];
    lv_obj_t *history_value_labels[SPORT_HISTORY_COUNT];
    lv_obj_t *history_unit_labels[SPORT_HISTORY_COUNT];

    lv_obj_t *goal_title;
    lv_obj_t *goal_value;
    lv_obj_t *goal_unit;
    lv_obj_t *goal_completed;
    lv_obj_t *goal_percent;
    lv_obj_t *goal_arc;
    lv_obj_t *goal_edit_label;

    lv_obj_t *goal_editor_unit;
    lv_obj_t *goal_input_value;
    lv_obj_t *goal_input_hint;

    lv_obj_t *heart_value;
    lv_obj_t *heart_threshold;

    lv_timer_t *data_timer;
    lv_timer_t *enter_timer;
    lv_timer_t *chart_bubble_timer;

    openvela_ui_sport_fonts_t fonts;
    openvela_ui_sport_event_cb_t event_cb;
    void *event_user_data;

    int32_t width;
    int32_t height;
    int32_t scale_1000;
    int32_t entry_cat_y;
    int32_t exit_cat_y;
    int32_t heart_x;
    int32_t heart_y;
    uint32_t heart_base_scale;

    openvela_ui_sport_page_t page;
    openvela_ui_sport_page_t pending_page;
    sport_stats_view_t stats_view;
    sport_stats_view_t pending_stats_view;
    openvela_ui_sport_event_t pending_event;
    int8_t transition_direction;

    bool visible;
    bool transitioning;
    bool pending_event_valid;
    bool special_active;
    bool confirm_visible;
    bool celebration_visible;
    bool celebration_pending;
    bool destroying;
    bool external_steps;
    bool external_heart;
    bool goal_input_fresh;
    bool persist_dirty;
    bool chart_touching;
    bool chart_touch_seen;
    bool low_power;

    char action_preview_path[SPORT_PATH_SIZE];
    uint32_t steps;
    uint32_t goals[SPORT_METRIC_COUNT];
    uint32_t goal_input;
    sport_history_record_t history[SPORT_HISTORY_STORED_COUNT];
    uint32_t day_samples[SPORT_HOURLY_SAMPLES];
    bool day_samples_valid;
    uint32_t hour_steps[SPORT_CHART_POINTS];
    uint8_t hour_point_count;
    uint8_t active_hour;
    uint8_t chart_clickable_hour;
    uint8_t chart_dot_count;
    int8_t chart_bubble_hour;
    uint16_t heart_rate;
    uint32_t random_state;
    uint32_t last_step_tick;
    uint32_t last_heart_tick;
    uint32_t last_motion_tick;
    uint32_t last_chart_touch_tick;
    bool motion_phase;
    uint8_t selected_history;
    char data_day[9];
    char last_celebration_day[9];
};

static void sport_transition_begin_async(void *data);
static void sport_event_async(void *data);
static void sport_apply_motion_frame(openvela_ui_sport_t *sport);
static void sport_enter_clicked(lv_event_t *event);
static void sport_exit_clicked(lv_event_t *event);
static void sport_exit_cancel_clicked(lv_event_t *event);
static void sport_exit_confirm_clicked(lv_event_t *event);
static void sport_history_row_clicked(lv_event_t *event);
static void sport_goal_edit_clicked(lv_event_t *event);
static void sport_goal_key_clicked(lv_event_t *event);
static void sport_goal_confirm_clicked(lv_event_t *event);
static void sport_celebration_clicked(lv_event_t *event);
static void sport_chart_hour_clicked(lv_event_t *event);
static void sport_chart_scroll_event(lv_event_t *event);
static void sport_chart_bubble_timer_cb(lv_timer_t *timer);
static void sport_update_history(openvela_ui_sport_t *sport);
static void sport_update_history_detail(openvela_ui_sport_t *sport);
static void sport_update_goal(openvela_ui_sport_t *sport);
static void sport_update_goal_editor(openvela_ui_sport_t *sport);
static void sport_check_celebration(openvela_ui_sport_t *sport);
static void sport_save_state(openvela_ui_sport_t *sport);
static bool sport_queue_stats_view(openvela_ui_sport_t *sport,
                                   sport_stats_view_t view, int direction);

static int32_t sport_sx(const openvela_ui_sport_t *sport, int32_t value)
{
    return (int32_t)(((int64_t)value * sport->scale_1000 + 500) / 1000);
}

static const lv_font_t *sport_font(const lv_font_t *font)
{
    return font ? font : LV_FONT_DEFAULT;
}

static bool sport_has_chinese(const openvela_ui_sport_t *sport)
{
    return sport->fonts.body != NULL;
}

static const uint32_t g_sport_demo_history_steps[SPORT_HISTORY_COUNT - 1] = {
    7352U, 10184U, 6230U, 8976U, 5421U, 11308U
};

static const uint32_t
g_sport_demo_history_goals[SPORT_METRIC_COUNT][SPORT_HISTORY_COUNT - 1] = {
    {8000U, 11000U, 7500U, 10000U, 6000U, 12000U},
    {400U, 500U, 350U, 400U, 300U, 450U},
    {30U, 60U, 35U, 40U, 30U, 45U},
};

static const uint8_t g_sport_history_progress[SPORT_HOURLY_SAMPLES] = {
    0, 0, 0, 0, 0, 0, 2, 7, 13, 19, 25, 31,
    39, 46, 53, 61, 68, 75, 82, 88, 93, 97, 100, 100
};

static int sport_metric_index(openvela_ui_sport_page_t page)
{
    if (page == OPENVELA_UI_SPORT_PAGE_CALORIES) {
        return 1;
    }
    if (page == OPENVELA_UI_SPORT_PAGE_DURATION) {
        return 2;
    }
    return 0;
}

static uint32_t sport_goal_default(int metric)
{
    static const uint32_t defaults[SPORT_METRIC_COUNT] = {
        8000U, 400U, 30U
    };

    return defaults[metric >= 0 && metric < SPORT_METRIC_COUNT ? metric : 0];
}

static uint32_t sport_goal_minimum(int metric)
{
    static const uint32_t minimums[SPORT_METRIC_COUNT] = {
        1000U, 50U, 5U
    };

    return minimums[metric >= 0 && metric < SPORT_METRIC_COUNT ? metric : 0];
}

static uint32_t sport_goal_maximum(int metric)
{
    static const uint32_t maximums[SPORT_METRIC_COUNT] = {
        99999U, 9999U, 1440U
    };

    return maximums[metric >= 0 && metric < SPORT_METRIC_COUNT ? metric : 0];
}

static const char *sport_metric_unit(openvela_ui_sport_page_t page,
                                     bool chinese)
{
    if (page == OPENVELA_UI_SPORT_PAGE_CALORIES) {
        return chinese ? "千卡" : "KCAL";
    }
    if (page == OPENVELA_UI_SPORT_PAGE_DURATION) {
        return chinese ? "分钟" : "MIN";
    }
    return chinese ? "步" : "STEPS";
}

static const char *sport_metric_name(openvela_ui_sport_page_t page,
                                     bool chinese)
{
    if (page == OPENVELA_UI_SPORT_PAGE_CALORIES) {
        return chinese ? "卡路里" : "CALORIES";
    }
    if (page == OPENVELA_UI_SPORT_PAGE_DURATION) {
        return chinese ? "运动时长" : "DURATION";
    }
    return chinese ? "步数" : "STEPS";
}

static bool sport_day_key_valid(const char *key)
{
    int index;
    int year;
    int month;
    int day;

    if (!key || strlen(key) != 8) {
        return false;
    }
    for (index = 0; index < 8; index++) {
        if (key[index] < '0' || key[index] > '9') {
            return false;
        }
    }
    year = (key[0] - '0') * 1000 + (key[1] - '0') * 100 +
           (key[2] - '0') * 10 + (key[3] - '0');
    month = (key[4] - '0') * 10 + (key[5] - '0');
    day = (key[6] - '0') * 10 + (key[7] - '0');
    return year >= SPORT_CLOCK_VALID_YEAR && year <= 9999 &&
           month >= 1 && month <= 12 && day >= 1 && day <= 31;
}

static bool sport_day_key_before(int days_before, char key[9])
{
    time_t now = time(NULL);
    time_t target;
    struct tm local;
    int year;

    if (days_before < 0) {
        key[0] = '\0';
        return false;
    }
    target = now - (time_t)days_before * 24 * 60 * 60;
    if (!localtime_r(&target, &local)) {
        key[0] = '\0';
        return false;
    }
    year = local.tm_year + 1900;
    if (year < SPORT_CLOCK_VALID_YEAR || year > 9999) {
        key[0] = '\0';
        return false;
    }
    snprintf(key, 9, "%04d%02d%02d",
             year, local.tm_mon + 1, local.tm_mday);
    return true;
}

static bool sport_today_key(char key[9])
{
    return sport_day_key_before(0, key);
}

static void sport_date_label(int days_before, char *buffer, size_t size)
{
    time_t now = time(NULL) - (time_t)days_before * 24 * 60 * 60;
    struct tm local;

    if (!localtime_r(&now, &local)) {
        snprintf(buffer, size, "--");
        return;
    }
    snprintf(buffer, size, "%02d月%02d日", local.tm_mon + 1,
             local.tm_mday);
}

static sport_history_record_t *sport_history_find(
    openvela_ui_sport_t *sport, const char *day)
{
    int index;

    for (index = 0; index < SPORT_HISTORY_STORED_COUNT; index++) {
        if (sport->history[index].valid &&
            strcmp(sport->history[index].day, day) == 0) {
            return &sport->history[index];
        }
    }
    return NULL;
}

static sport_history_record_t *sport_history_ensure(
    openvela_ui_sport_t *sport, const char *day)
{
    sport_history_record_t *record;
    int oldest = 0;
    int index;

    record = sport_history_find(sport, day);
    if (record) {
        return record;
    }
    for (index = 0; index < SPORT_HISTORY_STORED_COUNT; index++) {
        if (!sport->history[index].valid) {
            record = &sport->history[index];
            memset(record, 0, sizeof(*record));
            record->valid = true;
            snprintf(record->day, sizeof(record->day), "%s", day);
            return record;
        }
        if (strcmp(sport->history[index].day,
                   sport->history[oldest].day) < 0) {
            oldest = index;
        }
    }

    record = &sport->history[oldest];
    memset(record, 0, sizeof(*record));
    record->valid = true;
    snprintf(record->day, sizeof(record->day), "%s", day);
    return record;
}

static void sport_history_normalize_samples(sport_history_record_t *record)
{
    uint32_t previous = 0U;
    int index;

    for (index = 0; index < SPORT_HOURLY_SAMPLES; index++) {
        uint32_t value = record->samples[index];

        if (value > record->steps) {
            value = record->steps;
        }
        if (value < previous) {
            value = previous;
        }
        record->samples[index] = value;
        previous = value;
    }
    record->samples[SPORT_HOURLY_SAMPLES - 1] = record->steps;
    record->samples_valid = true;
}

static void sport_history_synthesize_samples(sport_history_record_t *record)
{
    int index;

    for (index = 0; index < SPORT_HOURLY_SAMPLES; index++) {
        record->samples[index] = (uint32_t)(
            ((uint64_t)record->steps * g_sport_history_progress[index]) /
            100U);
    }
    sport_history_normalize_samples(record);
}

static void sport_expand_legacy_samples(
    uint32_t samples[SPORT_HOURLY_SAMPLES],
    const unsigned long legacy[SPORT_LEGACY_SAMPLES])
{
    uint32_t left = 0U;
    int pair;

    memset(samples, 0, sizeof(uint32_t) * SPORT_HOURLY_SAMPLES);
    for (pair = 0; pair < SPORT_LEGACY_SAMPLES; pair++) {
        uint32_t right = legacy[pair] > SPORT_MAX_STEPS ?
            SPORT_MAX_STEPS : (uint32_t)legacy[pair];
        int left_boundary = pair * 2;
        int boundary;

        if (right < left) {
            right = left;
        }
        /* The legacy value belongs to the even boundary 02, 04 ... 24.
         * Fill the missing odd boundary by linear interpolation. */
        for (boundary = left_boundary + 1;
             boundary <= left_boundary + 2; boundary++) {
            uint64_t delta = (uint64_t)(right - left) *
                             (uint32_t)(boundary - left_boundary);

            samples[boundary - 1] = left + (uint32_t)(delta / 2U);
        }
        left = right;
    }
}

static sport_history_record_t *sport_history_store(
    openvela_ui_sport_t *sport, const char *day, uint32_t steps,
    const uint32_t goals[SPORT_METRIC_COUNT], const uint32_t *samples)
{
    sport_history_record_t *record;
    int metric;

    if (!sport_day_key_valid(day)) {
        return NULL;
    }
    record = sport_history_ensure(sport, day);
    record->steps = steps > SPORT_MAX_STEPS ? SPORT_MAX_STEPS : steps;
    for (metric = 0; metric < SPORT_METRIC_COUNT; metric++) {
        record->goals[metric] = goals[metric];
    }
    if (samples) {
        memcpy(record->samples, samples, sizeof(record->samples));
        record->samples_valid = true;
    }
    if (record->samples_valid) {
        sport_history_normalize_samples(record);
    } else {
        sport_history_synthesize_samples(record);
    }
    return record;
}

static sport_history_record_t *sport_history_for_offset(
    openvela_ui_sport_t *sport, int days_before)
{
    char day[9];

    if (days_before <= 0 || days_before >= SPORT_HISTORY_COUNT ||
        !sport_day_key_before(days_before, day)) {
        return NULL;
    }
    return sport_history_find(sport, day);
}

static void sport_sync_day_samples(openvela_ui_sport_t *sport)
{
    uint32_t previous = 0U;
    int latest_hour = sport->active_hour;
    int index;

    if (latest_hour >= SPORT_HOURLY_SAMPLES) {
        latest_hour = SPORT_HOURLY_SAMPLES - 1;
    }
    for (index = 0; index < SPORT_HOURLY_SAMPLES; index++) {
        if (index <= latest_hour) {
            uint32_t value = sport->hour_steps[index + 1];

            if (value > sport->steps) {
                value = sport->steps;
            }
            if (value < previous) {
                value = previous;
            }
            sport->day_samples[index] = value;
            previous = value;
        }
    }
    sport->day_samples_valid = true;
}

static void sport_history_capture_current(openvela_ui_sport_t *sport)
{
    uint32_t samples[SPORT_HOURLY_SAMPLES];

    if (!sport_day_key_valid(sport->data_day)) {
        return;
    }
    sport_sync_day_samples(sport);
    memcpy(samples, sport->day_samples, sizeof(samples));
    samples[SPORT_HOURLY_SAMPLES - 1] = sport->steps;
    sport_history_store(sport, sport->data_day, sport->steps,
                        sport->goals, samples);
}

static bool sport_parse_unsigned_list(const char *text,
                                      unsigned long *values,
                                      size_t count)
{
    const char *cursor = text;
    char *end;
    size_t index;

    for (index = 0; index < count; index++) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        errno = 0;
        values[index] = strtoul(cursor, &end, 10);
        if (end == cursor || errno == ERANGE) {
            return false;
        }
        cursor = end;
        if (index + 1 < count) {
            if (*cursor != ',') {
                return false;
            }
            cursor++;
        }
    }
    while (*cursor == ' ' || *cursor == '\t' ||
           *cursor == '\r' || *cursor == '\n') {
        cursor++;
    }
    return *cursor == '\0';
}

static bool sport_parse_history_line(const char *line, const char *prefix,
                                     char day[9], unsigned long *values,
                                     size_t count)
{
    const char *payload;
    const char *comma;
    size_t prefix_length = strlen(prefix);

    if (strncmp(line, prefix, prefix_length) != 0) {
        return false;
    }
    payload = line + prefix_length;
    comma = strchr(payload, ',');
    if (!comma || comma - payload != 8) {
        return false;
    }
    memcpy(day, payload, 8);
    day[8] = '\0';
    return sport_day_key_valid(day) &&
           sport_parse_unsigned_list(comma + 1, values, count);
}

static void sport_load_state(openvela_ui_sport_t *sport)
{
    FILE *file;
    char line[512];
    char today[9];
    char stored_day[9] = "";
    unsigned long stored_steps = 0;
    bool stored_steps_valid = false;
    bool clock_valid;
    int metric;

    clock_valid = sport_today_key(today);
    snprintf(sport->data_day, sizeof(sport->data_day), "%s", today);
    for (metric = 0; metric < SPORT_METRIC_COUNT; metric++) {
        sport->goals[metric] = sport_goal_default(metric);
    }
    sport->steps = 3268U;

    file = fopen(SPORT_STATE_PATH, "r");
    if (!file) {
        sport->persist_dirty = true;
        return;
    }

    while (fgets(line, sizeof(line), file)) {
        unsigned long value;
        char text[16];
        char history_day[9];
        unsigned long history_values[SPORT_HOURLY_SAMPLES];

        if (sscanf(line, "step_goal=%lu", &value) == 1 &&
            value >= sport_goal_minimum(0) &&
            value <= sport_goal_maximum(0)) {
            sport->goals[0] = (uint32_t)value;
        } else if (sscanf(line, "calorie_goal=%lu", &value) == 1 &&
                   value >= sport_goal_minimum(1) &&
                   value <= sport_goal_maximum(1)) {
            sport->goals[1] = (uint32_t)value;
        } else if (sscanf(line, "duration_goal=%lu", &value) == 1 &&
                   value >= sport_goal_minimum(2) &&
                   value <= sport_goal_maximum(2)) {
            sport->goals[2] = (uint32_t)value;
        } else if (sscanf(line, "steps=%lu", &value) == 1) {
            stored_steps = value;
            stored_steps_valid = true;
        } else if (sscanf(line, "data_day=%8s", text) == 1) {
            snprintf(stored_day, sizeof(stored_day), "%s", text);
        } else if (sscanf(line, "celebration_day=%8s", text) == 1) {
            snprintf(sport->last_celebration_day,
                     sizeof(sport->last_celebration_day), "%s", text);
        } else if (strncmp(line, "day_hours=", 10) == 0) {
            int index;

            if (!sport_parse_unsigned_list(line + 10, history_values,
                                            SPORT_HOURLY_SAMPLES)) {
                sport->persist_dirty = true;
                continue;
            }
            for (index = 0; index < SPORT_HOURLY_SAMPLES; index++) {
                if (history_values[index] > SPORT_MAX_STEPS) {
                    break;
                }
                sport->day_samples[index] =
                    (uint32_t)history_values[index];
            }
            if (index == SPORT_HOURLY_SAMPLES) {
                sport->day_samples_valid = true;
            } else {
                memset(sport->day_samples, 0,
                       sizeof(sport->day_samples));
                sport->day_samples_valid = false;
                sport->persist_dirty = true;
            }
        } else if (strncmp(line, "day_samples=", 12) == 0) {
            int index;

            if (!sport_parse_unsigned_list(line + 12, history_values,
                                            SPORT_LEGACY_SAMPLES)) {
                sport->persist_dirty = true;
                continue;
            }
            for (index = 0; index < SPORT_LEGACY_SAMPLES; index++) {
                if (history_values[index] > SPORT_MAX_STEPS) {
                    break;
                }
            }
            if (index == SPORT_LEGACY_SAMPLES) {
                sport_expand_legacy_samples(sport->day_samples,
                                            history_values);
                sport->day_samples_valid = true;
            } else {
                memset(sport->day_samples, 0,
                       sizeof(sport->day_samples));
                sport->day_samples_valid = false;
            }
            /* A successful legacy read is still dirty: the next save writes
             * the full 24-hour representation. */
            sport->persist_dirty = true;
        } else if (strncmp(line, "history=", 8) == 0) {
            uint32_t history_goals[SPORT_METRIC_COUNT];

            if (!sport_parse_history_line(line, "history=", history_day,
                                          history_values, 4) ||
                history_values[0] > SPORT_MAX_STEPS ||
                history_values[1] < sport_goal_minimum(0) ||
                history_values[1] > sport_goal_maximum(0) ||
                history_values[2] < sport_goal_minimum(1) ||
                history_values[2] > sport_goal_maximum(1) ||
                history_values[3] < sport_goal_minimum(2) ||
                history_values[3] > sport_goal_maximum(2)) {
                sport->persist_dirty = true;
                continue;
            }
            history_goals[0] = (uint32_t)history_values[1];
            history_goals[1] = (uint32_t)history_values[2];
            history_goals[2] = (uint32_t)history_values[3];
            sport_history_store(sport, history_day,
                                (uint32_t)history_values[0],
                                history_goals, NULL);
        } else if (strncmp(line, "history_hours=", 14) == 0) {
            sport_history_record_t *record;
            uint32_t samples[SPORT_HOURLY_SAMPLES];
            uint32_t fallback_goals[SPORT_METRIC_COUNT];
            int index;

            if (!sport_parse_history_line(line, "history_hours=",
                                          history_day, history_values,
                                          SPORT_HOURLY_SAMPLES)) {
                sport->persist_dirty = true;
                continue;
            }
            for (index = 0; index < SPORT_HOURLY_SAMPLES; index++) {
                if (history_values[index] > SPORT_MAX_STEPS) {
                    break;
                }
                samples[index] = (uint32_t)history_values[index];
            }
            if (index != SPORT_HOURLY_SAMPLES) {
                sport->persist_dirty = true;
                continue;
            }
            record = sport_history_find(sport, history_day);
            if (record) {
                memcpy(record->samples, samples, sizeof(samples));
                record->samples_valid = true;
                sport_history_normalize_samples(record);
            } else {
                for (metric = 0; metric < SPORT_METRIC_COUNT; metric++) {
                    fallback_goals[metric] = sport_goal_default(metric);
                }
                sport_history_store(
                    sport, history_day,
                    samples[SPORT_HOURLY_SAMPLES - 1],
                    fallback_goals, samples);
                sport->persist_dirty = true;
            }
        } else if (strncmp(line, "history_samples=", 16) == 0) {
            sport_history_record_t *record;
            uint32_t samples[SPORT_HOURLY_SAMPLES];
            uint32_t fallback_goals[SPORT_METRIC_COUNT];
            int index;

            if (!sport_parse_history_line(line, "history_samples=",
                                          history_day, history_values,
                                          SPORT_LEGACY_SAMPLES)) {
                sport->persist_dirty = true;
                continue;
            }
            for (index = 0; index < SPORT_LEGACY_SAMPLES; index++) {
                if (history_values[index] > SPORT_MAX_STEPS) {
                    break;
                }
            }
            if (index != SPORT_LEGACY_SAMPLES) {
                sport->persist_dirty = true;
                continue;
            }
            sport_expand_legacy_samples(samples, history_values);
            record = sport_history_find(sport, history_day);
            if (record) {
                memcpy(record->samples, samples, sizeof(samples));
                record->samples_valid = true;
                sport_history_normalize_samples(record);
            } else {
                for (metric = 0; metric < SPORT_METRIC_COUNT; metric++) {
                    fallback_goals[metric] = sport_goal_default(metric);
                }
                sport_history_store(
                    sport, history_day,
                    samples[SPORT_HOURLY_SAMPLES - 1],
                    fallback_goals, samples);
            }
            sport->persist_dirty = true;
        }
    }
    fclose(file);

    if (sport_day_key_valid(stored_day) && stored_steps_valid &&
        stored_steps <= SPORT_MAX_STEPS) {
        snprintf(sport->data_day, sizeof(sport->data_day), "%s", stored_day);
        sport->steps = (uint32_t)stored_steps;
        if (sport->day_samples_valid) {
            uint32_t previous = 0U;
            int index;

            for (index = 0; index < SPORT_HOURLY_SAMPLES; index++) {
                if (sport->day_samples[index] > sport->steps) {
                    sport->day_samples[index] = sport->steps;
                    sport->persist_dirty = true;
                }
                if (sport->day_samples[index] < previous) {
                    sport->day_samples[index] = previous;
                    sport->persist_dirty = true;
                }
                previous = sport->day_samples[index];
            }
        }
        if (clock_valid && strcmp(stored_day, today) < 0) {
            sport_history_store(sport, stored_day, sport->steps,
                                sport->goals,
                                sport->day_samples_valid ?
                                    sport->day_samples : NULL);
            snprintf(sport->data_day, sizeof(sport->data_day), "%s", today);
            sport->steps = 0U;
            memset(sport->day_samples, 0, sizeof(sport->day_samples));
            sport->day_samples_valid = false;
            sport->persist_dirty = true;
            sport_save_state(sport);
        }
    } else if (clock_valid) {
        snprintf(sport->data_day, sizeof(sport->data_day), "%s", today);
        if (stored_day[0] != '\0' || stored_steps_valid) {
            sport->steps = 0U;
        }
        memset(sport->day_samples, 0, sizeof(sport->day_samples));
        sport->day_samples_valid = false;
        sport->persist_dirty = true;
    } else {
        sport->persist_dirty = true;
    }
}

static int sport_write_hour_line(FILE *file, const char *prefix,
                                 const char *day,
                                 const uint32_t samples[SPORT_HOURLY_SAMPLES])
{
    int index;

    if (fprintf(file, "%s", prefix) < 0) {
        return -1;
    }
    if (day && fprintf(file, "%s,", day) < 0) {
        return -1;
    }
    for (index = 0; index < SPORT_HOURLY_SAMPLES; index++) {
        if (fprintf(file, index + 1 < SPORT_HOURLY_SAMPLES ? "%lu," :
                                                               "%lu\n",
                    (unsigned long)samples[index]) < 0) {
            return -1;
        }
    }
    return 0;
}

static void sport_save_state(openvela_ui_sport_t *sport)
{
    FILE *file;
    int write_result;
    int close_result;
    int index;
    char today[9];

    if (!sport || !sport->persist_dirty) {
        return;
    }

    if (sport_today_key(today) &&
        strcmp(today, sport->data_day) == 0) {
        sport_sync_day_samples(sport);
    }

    mkdir("/data/etc", 0777);
    mkdir("/data/etc/openvela_ui", 0777);
    file = fopen(SPORT_STATE_TMP_PATH, "w");
    if (!file) {
        return;
    }
    write_result = fprintf(file,
                           "data_day=%s\nsteps=%lu\nstep_goal=%lu\n"
                           "calorie_goal=%lu\nduration_goal=%lu\n"
                           "celebration_day=%s\n",
                           sport->data_day, (unsigned long)sport->steps,
                           (unsigned long)sport->goals[0],
                           (unsigned long)sport->goals[1],
                           (unsigned long)sport->goals[2],
                           sport->last_celebration_day);
    if (write_result >= 0 && sport->day_samples_valid &&
        sport_write_hour_line(file, "day_hours=", NULL,
                              sport->day_samples) < 0) {
        write_result = -1;
    }
    for (index = 0;
         write_result >= 0 && index < SPORT_HISTORY_STORED_COUNT;
         index++) {
        sport_history_record_t *record = &sport->history[index];

        if (!record->valid || !sport_day_key_valid(record->day)) {
            continue;
        }
        if (fprintf(file,
                    "history=%s,%lu,%lu,%lu,%lu\n",
                    record->day,
                    (unsigned long)record->steps,
                    (unsigned long)record->goals[0],
                    (unsigned long)record->goals[1],
                    (unsigned long)record->goals[2]) < 0 ||
            sport_write_hour_line(file, "history_hours=", record->day,
                                  record->samples) < 0) {
            write_result = -1;
        }
    }
    close_result = fclose(file);
    if (write_result < 0 || close_result != 0) {
        unlink(SPORT_STATE_TMP_PATH);
        return;
    }
    if (rename(SPORT_STATE_TMP_PATH, SPORT_STATE_PATH) != 0) {
        unlink(SPORT_STATE_TMP_PATH);
        return;
    }
    sport->persist_dirty = false;
}

static lv_obj_t *sport_box(openvela_ui_sport_t *sport, lv_obj_t *parent,
                           int32_t x, int32_t y, int32_t width,
                           int32_t height, uint32_t color, int32_t radius,
                           lv_opa_t opacity)
{
    lv_obj_t *object = lv_obj_create(parent);

    lv_obj_remove_style_all(object);
    lv_obj_set_pos(object, sport_sx(sport, x), sport_sx(sport, y));
    lv_obj_set_size(object, sport_sx(sport, width), sport_sx(sport, height));
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, opacity, 0);
    lv_obj_set_style_radius(object, sport_sx(sport, radius), 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return object;
}

static lv_obj_t *sport_label(openvela_ui_sport_t *sport, lv_obj_t *parent,
                             const char *text, const lv_font_t *font,
                             uint32_t color, int32_t x, int32_t y,
                             int32_t width, lv_text_align_t align)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_remove_style_all(label);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_pos(label, sport_sx(sport, x), sport_sx(sport, y));
    lv_obj_set_width(label, sport_sx(sport, width));
    lv_obj_set_style_text_font(label, sport_font(font), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(label, align, 0);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return label;
}

static lv_obj_t *sport_image(openvela_ui_sport_t *sport, lv_obj_t *parent,
                             const char *path, int32_t x, int32_t y,
                             int32_t source_width, int32_t display_width)
{
    lv_obj_t *image;
    uint32_t scale;

    if (!path || !path[0] || access(path, R_OK) != 0) {
        return NULL;
    }

    image = lv_image_create(parent);
    lv_image_set_src(image, path);
    lv_image_set_pivot(image, 0, 0);
    scale = (uint32_t)((sport_sx(sport, display_width) * 256 +
                        source_width / 2) / source_width);
    lv_image_set_scale(image, scale);
    lv_obj_set_pos(image, sport_sx(sport, x), sport_sx(sport, y));
    lv_obj_clear_flag(image, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return image;
}

static lv_obj_t *sport_page_create(openvela_ui_sport_t *sport)
{
    lv_obj_t *page = lv_obj_create(sport->root);

    lv_obj_remove_style_all(page);
    lv_obj_set_pos(page, 0, 0);
    lv_obj_set_size(page, sport->width, sport->height);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(page, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return page;
}

static void sport_set_hidden(lv_obj_t *object, bool hidden)
{
    if (!object || !lv_obj_is_valid(object)) {
        return;
    }

    if (hidden) {
        lv_obj_add_flag(object, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(object, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t *sport_make_icon_fallback(openvela_ui_sport_t *sport,
                                          lv_obj_t *parent,
                                          int32_t x, int32_t y,
                                          int32_t size, const char *text,
                                          uint32_t color)
{
    lv_obj_t *icon = sport_box(sport, parent, x, y, size, size,
                               color, size / 2, LV_OPA_COVER);

    lv_obj_set_style_border_color(icon, lv_color_hex(0x4388f2), 0);
    lv_obj_set_style_border_width(icon, sport_sx(sport, 4), 0);
    sport_label(sport, icon, text, sport->fonts.title, 0xffffff,
                0, size / 3, size, LV_TEXT_ALIGN_CENTER);
    return icon;
}

static lv_obj_t *sport_make_cat(openvela_ui_sport_t *sport,
                                lv_obj_t *parent, int32_t x, int32_t y,
                                int32_t width, lv_obj_t **image_out)
{
    lv_obj_t *visual = sport_image(sport, parent,
                                   sport->action_preview_path,
                                   x, y, 144, width);

    if (visual) {
        *image_out = visual;
        return visual;
    }

    *image_out = NULL;
    return sport_make_icon_fallback(sport, parent, x + width / 8, y,
                                    width * 3 / 4,
                                    sport_has_chinese(sport) ? "喵" : "CAT",
                                    0xd9b68f);
}

static void sport_create_background(openvela_ui_sport_t *sport)
{
    if (access(SPORT_BACKGROUND_PATH, R_OK) == 0) {
        sport->background = lv_image_create(sport->root);
        lv_image_set_src(sport->background, SPORT_BACKGROUND_PATH);
        lv_image_set_pivot(sport->background, 0, 0);
        lv_image_set_scale(sport->background,
                           (uint32_t)(sport->width * 256 /
                                      SPORT_DESIGN_WIDTH));
        lv_obj_set_pos(sport->background, 0, 0);
        lv_obj_clear_flag(sport->background,
                          LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        return;
    }

    sport_box(sport, sport->root, 274, 46, 116, 224,
              0x231951, 54, LV_OPA_80);
    sport_box(sport, sport->root, 28, 385, 376, 72,
              0x38205e, 36, LV_OPA_COVER);
}

static lv_obj_t *sport_make_button(openvela_ui_sport_t *sport,
                                   lv_obj_t *parent,
                                   int32_t x, int32_t y,
                                   int32_t width, int32_t height,
                                   uint32_t fill, uint32_t border,
                                   int32_t border_width,
                                   lv_event_cb_t callback)
{
    lv_obj_t *button = sport_box(sport, parent, x, y, width, height,
                                 fill, height / 2, LV_OPA_COVER);

    lv_obj_set_style_border_color(button, lv_color_hex(border), 0);
    lv_obj_set_style_border_width(button, sport_sx(sport, border_width), 0);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, sport);
    return button;
}

static void sport_create_entry_page(openvela_ui_sport_t *sport)
{
    lv_obj_t *jump;

    sport->entry_page = sport_page_create(sport);
    sport_label(sport, sport->entry_page,
                sport_has_chinese(sport) ? "运动模式" : "SPORT MODE",
                sport->fonts.title, 0xffffff,
                66, 36, 300, LV_TEXT_ALIGN_CENTER);

    jump = sport_image(sport, sport->entry_page, SPORT_JUMP_ROPE_PATH,
                       152, 112, 144, 128);
    if (!jump) {
        sport_make_icon_fallback(sport, sport->entry_page,
                                 152, 112, 128,
                                 sport_has_chinese(sport) ? "跳" : "GO",
                                 0x6559ef);
    }

    sport->entry_cat_y = sport_sx(sport, 237);
    sport->entry_cat_visual = sport_make_cat(sport, sport->entry_page,
                                             136, 237, 160,
                                             &sport->entry_cat_image);

    sport->enter_button = sport_make_button(sport, sport->entry_page,
                                             61, 408, 310, 62,
                                             0xf7f3ff, 0x739cff, 4,
                                             sport_enter_clicked);
    sport->enter_button_label = sport_label(sport, sport->enter_button,
        sport_has_chinese(sport) ? "进入运动" : "START",
        sport->fonts.body, 0x142a65,
        12, 15, 286, LV_TEXT_ALIGN_CENTER);
}

static void sport_create_stats_page(openvela_ui_sport_t *sport)
{
    lv_obj_t *card;
    int index;

    sport->stats_page = sport_page_create(sport);
    sport_box(sport, sport->stats_page, 0, 0, 432, 514,
              0xffffff, 0, LV_OPA_10);
    sport->stats_title = sport_label(sport, sport->stats_page, "",
                                     sport->fonts.body, 0xffffff,
                                     86, 17, 260,
                                     LV_TEXT_ALIGN_CENTER);

    card = sport_box(sport, sport->stats_page, 24, 76, 384, 260,
                     0xffffff, 30, (lv_opa_t)242);
    sport->chart_heading = sport_label(
        sport, card, sport_has_chinese(sport) ? "今日趋势" : "TODAY",
        sport->fonts.body, 0x142a65,
        29, 13, 160, LV_TEXT_ALIGN_LEFT);
    sport->chart_goal_text = sport_label(
        sport, card, "", sport->fonts.small, 0x7a6a36,
        180, 20, 175, LV_TEXT_ALIGN_RIGHT);
    sport_set_hidden(sport->chart_goal_text, true);

    sport->chart_scroll = sport_box(
        sport, card, 23, 54, SPORT_CHART_VIEW_WIDTH, 171,
        0xffffff, 0, LV_OPA_TRANSP);
    lv_obj_add_flag(sport->chart_scroll,
                    LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(sport->chart_scroll,
                      LV_OBJ_FLAG_SCROLL_ELASTIC |
                      LV_OBJ_FLAG_SCROLL_CHAIN |
                      LV_OBJ_FLAG_EVENT_BUBBLE |
                      LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_scroll_dir(sport->chart_scroll, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(sport->chart_scroll, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(sport->chart_scroll, 0, 0);
    lv_obj_add_event_cb(sport->chart_scroll, sport_chart_scroll_event,
                        LV_EVENT_ALL, sport);

    sport->chart_content = sport_box(
        sport, sport->chart_scroll, 0, 0,
        SPORT_CHART_CONTENT_WIDTH, 171,
        0xffffff, 0, LV_OPA_TRANSP);
    sport_box(sport, sport->chart_content, 0, 16,
              SPORT_CHART_CONTENT_WIDTH, 2,
              0xd8e9fb, 0, LV_OPA_COVER);
    sport_box(sport, sport->chart_content, 0, 53,
              SPORT_CHART_CONTENT_WIDTH, 2,
              0xd8e9fb, 0, LV_OPA_COVER);
    sport_box(sport, sport->chart_content, 0, 90,
              SPORT_CHART_CONTENT_WIDTH, 2,
              0xd8e9fb, 0, LV_OPA_COVER);
    sport_box(sport, sport->chart_content, 0, 127,
              SPORT_CHART_CONTENT_WIDTH, 2,
              0xd8e9fb, 0, LV_OPA_COVER);

    sport->chart_goal_line = sport_box(
        sport, sport->chart_content, 0, 127,
        SPORT_CHART_CONTENT_WIDTH, 4,
        0xffd020, 2, LV_OPA_COVER);
    sport_set_hidden(sport->chart_goal_line, true);

    sport->chart_line = lv_line_create(sport->chart_content);
    lv_obj_remove_style_all(sport->chart_line);
    lv_obj_set_pos(sport->chart_line, 0, 0);
    lv_obj_set_size(sport->chart_line,
                    sport_sx(sport, SPORT_CHART_CONTENT_WIDTH),
                    sport_sx(sport, 140));
    lv_obj_set_style_line_color(sport->chart_line,
                                lv_color_hex(0x347ff0), 0);
    lv_obj_set_style_line_width(sport->chart_line,
                                sport_sx(sport, 5), 0);
    lv_obj_set_style_line_rounded(sport->chart_line, true, 0);
    lv_obj_clear_flag(sport->chart_line,
                      LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    for (index = 0; index < SPORT_CHART_POINTS; index++) {
        char hour[8];
        uint32_t label_color = index % 3 == 0 ? 0x142a65 : 0x8b98ae;

        sport->chart_dots[index] = sport_box(sport, sport->chart_content,
                                             0, 0, 9, 9,
                                             0xffd24d, 5,
                                             LV_OPA_COVER);
        lv_obj_set_style_border_color(sport->chart_dots[index],
                                      lv_color_hex(0x347ff0), 0);
        lv_obj_set_style_border_width(sport->chart_dots[index],
                                      sport_sx(sport, 2), 0);
        snprintf(hour, sizeof(hour), "%02d", index);
        sport->chart_hour_labels[index] = sport_label(
            sport, sport->chart_content, hour, sport->fonts.small,
            label_color, index * SPORT_CHART_HOUR_WIDTH,
            141, SPORT_CHART_HOUR_WIDTH, LV_TEXT_ALIGN_CENTER);
        lv_label_set_long_mode(sport->chart_hour_labels[index],
                               LV_LABEL_LONG_CLIP);

        sport->chart_hour_targets[index] = sport_box(
            sport, sport->chart_content,
            index * SPORT_CHART_HOUR_WIDTH, 0,
            SPORT_CHART_HOUR_WIDTH, 171,
            0xffffff, 0, LV_OPA_TRANSP);
        lv_obj_set_user_data(sport->chart_hour_targets[index],
                             (void *)(intptr_t)index);
        lv_obj_add_flag(sport->chart_hour_targets[index],
                        LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(sport->chart_hour_targets[index],
                          LV_OBJ_FLAG_EVENT_BUBBLE |
                          LV_OBJ_FLAG_GESTURE_BUBBLE);
        lv_obj_add_event_cb(sport->chart_hour_targets[index],
                            sport_chart_scroll_event,
                            LV_EVENT_ALL, sport);
        lv_obj_add_event_cb(sport->chart_hour_targets[index],
                            sport_chart_hour_clicked,
                            LV_EVENT_CLICKED, sport);
    }

    sport->chart_bubble = sport_box(
        sport, sport->chart_content, 0, 0, 132, 34,
        0x142a65, 17, LV_OPA_COVER);
    sport->chart_bubble_label = sport_label(
        sport, sport->chart_bubble, "", sport->fonts.small, 0xffffff,
        3, 7, 126, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(sport->chart_bubble_label, LV_LABEL_LONG_CLIP);
    sport_set_hidden(sport->chart_bubble, true);
    sport->chart_bubble_timer = lv_timer_create(
        sport_chart_bubble_timer_cb, SPORT_BUBBLE_HIDE_MS, sport);
    if (sport->chart_bubble_timer) {
        lv_timer_pause(sport->chart_bubble_timer);
    }

    sport_label(sport, card,
                sport_has_chinese(sport) ?
                    "左右滑动查看全天" : "SWIPE FOR 24 HOURS",
                sport->fonts.small, 0x8292aa,
                82, 229, 220, LV_TEXT_ALIGN_CENTER);

    sport->stats_current_label = sport_label(sport, sport->stats_page, "",
                                              sport->fonts.body, 0xffffff,
                                              116, 348, 200,
                                              LV_TEXT_ALIGN_CENTER);
    sport->stats_value = sport_label(sport, sport->stats_page, "0",
                                     sport->fonts.clock, 0xffffff,
                                     34, 379, 260,
                                     LV_TEXT_ALIGN_RIGHT);
    lv_label_set_long_mode(sport->stats_value, LV_LABEL_LONG_CLIP);
    sport->stats_unit = sport_label(sport, sport->stats_page, "",
                                    sport->fonts.body, 0xffffff,
                                    302, 411, 108,
                                    LV_TEXT_ALIGN_LEFT);
    sport_label(sport, sport->stats_page,
                sport_has_chinese(sport) ?
                    "左滑历史 · 上下切换指标" :
                    "LEFT: HISTORY · UP/DOWN: METRIC",
                sport->fonts.small, 0xe2e8f6,
                42, 474, 348, LV_TEXT_ALIGN_CENTER);
    sport_set_hidden(sport->stats_page, true);
}

static void sport_create_history_page(openvela_ui_sport_t *sport)
{
    lv_obj_t *card;
    int index;

    sport->history_page = sport_page_create(sport);
    sport_box(sport, sport->history_page, 0, 0, 432, 514,
              0xffffff, 0, LV_OPA_10);
    sport->history_title = sport_label(sport, sport->history_page, "",
                                       sport->fonts.body, 0xffffff,
                                       70, 22, 292, LV_TEXT_ALIGN_CENTER);
    card = sport_box(sport, sport->history_page, 24, 76, 384, 374,
                     0xffffff, 30, (lv_opa_t)244);

    for (index = 0; index < SPORT_HISTORY_COUNT; index++) {
        lv_obj_t *row = sport_box(sport, card, 8, 7 + index * 51,
                                  368, 50, 0xffffff, 12, LV_OPA_TRANSP);

        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_user_data(row, (void *)(intptr_t)index);
        lv_obj_add_event_cb(row, sport_history_row_clicked,
                            LV_EVENT_CLICKED, sport);
        sport->history_rows[index] = row;
        sport->history_dots[index] = sport_box(
            sport, row, 12, 20, 11, 11,
            index == 0 ? 0xffca3a : 0xffffff, 6,
            index == 0 ? LV_OPA_COVER : LV_OPA_TRANSP);
        sport->history_date_labels[index] = sport_label(
            sport, row, "", sport->fonts.small, 0x263756,
            36, 13, 116, LV_TEXT_ALIGN_LEFT);
        sport->history_value_labels[index] = sport_label(
            sport, row, "0", sport->fonts.body, 0x142a65,
            146, 10, 166, LV_TEXT_ALIGN_RIGHT);
        lv_label_set_long_mode(sport->history_value_labels[index],
                               LV_LABEL_LONG_CLIP);
        sport->history_unit_labels[index] = sport_label(
            sport, row, "", sport->fonts.small, 0x71809c,
            318, 15, 42, LV_TEXT_ALIGN_LEFT);
        lv_label_set_long_mode(sport->history_unit_labels[index],
                               LV_LABEL_LONG_CLIP);
        if (index < SPORT_HISTORY_COUNT - 1) {
            sport_box(sport, row, 34, 48, 326, 1,
                      0xdfe6f1, 0, LV_OPA_COVER);
        }
    }
    sport_label(sport, sport->history_page,
                sport_has_chinese(sport) ?
                    "左滑查看目标 · 右滑返回" :
                    "LEFT: GOAL · RIGHT: BACK",
                sport->fonts.small, 0xe2e8f6,
                38, 466, 356, LV_TEXT_ALIGN_CENTER);
    sport_set_hidden(sport->history_page, true);
}

static void sport_create_goal_page(openvela_ui_sport_t *sport)
{
    lv_obj_t *card;
    lv_obj_t *button;

    sport->goal_page = sport_page_create(sport);
    sport_box(sport, sport->goal_page, 0, 0, 432, 514,
              0xffffff, 0, LV_OPA_10);
    sport->goal_title = sport_label(sport, sport->goal_page, "",
                                    sport->fonts.body, 0xffffff,
                                    70, 22, 292, LV_TEXT_ALIGN_CENTER);
    card = sport_box(sport, sport->goal_page, 24, 76, 384, 326,
                     0xffffff, 30, (lv_opa_t)244);
    sport_label(sport, card,
                sport_has_chinese(sport) ? "今日目标" : "TODAY GOAL",
                sport->fonts.body, 0x142a65,
                102, 14, 180, LV_TEXT_ALIGN_CENTER);
    sport->goal_value = sport_label(sport, card, "0",
                                    sport->fonts.title, 0x142a65,
                                    42, 47, 210, LV_TEXT_ALIGN_RIGHT);
    lv_label_set_long_mode(sport->goal_value, LV_LABEL_LONG_CLIP);
    sport->goal_unit = sport_label(sport, card, "",
                                   sport->fonts.small, 0x657590,
                                   260, 67, 72, LV_TEXT_ALIGN_LEFT);
    lv_label_set_long_mode(sport->goal_unit, LV_LABEL_LONG_CLIP);

    sport->goal_arc = lv_arc_create(card);
    lv_obj_set_pos(sport->goal_arc, sport_sx(sport, 97),
                   sport_sx(sport, 98));
    lv_obj_set_size(sport->goal_arc, sport_sx(sport, 190),
                    sport_sx(sport, 190));
    lv_arc_set_range(sport->goal_arc, 0, 100);
    lv_arc_set_bg_angles(sport->goal_arc, 0, 360);
    lv_obj_set_style_arc_width(sport->goal_arc, sport_sx(sport, 13),
                               LV_PART_MAIN);
    lv_obj_set_style_arc_color(sport->goal_arc, lv_color_hex(0xd9e2f1),
                               LV_PART_MAIN);
    lv_obj_set_style_arc_width(sport->goal_arc, sport_sx(sport, 13),
                               LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(sport->goal_arc, lv_color_hex(0x347ff0),
                               LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(sport->goal_arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_clear_flag(sport->goal_arc, LV_OBJ_FLAG_CLICKABLE);
    sport->goal_percent = sport_label(sport, card, "0%",
                                      sport->fonts.title, 0x347ff0,
                                      97, 170, 190, LV_TEXT_ALIGN_CENTER);
    sport->goal_completed = sport_label(
        sport, card, "", sport->fonts.small, 0x657590,
        32, 286, 320, LV_TEXT_ALIGN_CENTER);

    button = sport_make_button(sport, sport->goal_page,
                               51, 418, 330, 58,
                               0x347ff0, 0x86b6ff, 2,
                               sport_goal_edit_clicked);
    sport->goal_edit_label = sport_label(
        sport, button, "", sport->fonts.body, 0xffffff,
        12, 14, 306, LV_TEXT_ALIGN_CENTER);
    sport_label(sport, sport->goal_page,
                sport_has_chinese(sport) ?
                    "右滑返回历史" : "SWIPE RIGHT TO HISTORY",
                sport->fonts.small, 0xe2e8f6,
                56, 482, 320, LV_TEXT_ALIGN_CENTER);
    sport_set_hidden(sport->goal_page, true);
}

static lv_obj_t *sport_create_goal_key(openvela_ui_sport_t *sport,
                                       lv_obj_t *parent, int32_t x,
                                       int32_t y, int code,
                                       const char *text)
{
    lv_obj_t *button = sport_make_button(sport, parent, x, y, 92, 47,
                                         0xeef3fb, 0xa9bfdf, 2,
                                         sport_goal_key_clicked);

    lv_obj_set_user_data(button, (void *)(intptr_t)code);
    sport_label(sport, button, text, sport->fonts.body, 0x142a65,
                4, 10, 84, LV_TEXT_ALIGN_CENTER);
    return button;
}

static void sport_create_goal_editor_page(openvela_ui_sport_t *sport)
{
    lv_obj_t *card;
    lv_obj_t *confirm;
    int row;
    int column;
    int digit = 1;

    sport->goal_editor_page = sport_page_create(sport);
    sport_box(sport, sport->goal_editor_page, 0, 0, 432, 514,
              0xffffff, 0, LV_OPA_10);
    sport_label(sport, sport->goal_editor_page,
                sport_has_chinese(sport) ? "修改目标" : "EDIT GOAL",
                sport->fonts.body, 0xffffff,
                70, 17, 292, LV_TEXT_ALIGN_CENTER);
    card = sport_box(sport, sport->goal_editor_page,
                     24, 63, 384, 427,
                     0xffffff, 28, (lv_opa_t)246);
    sport_label(sport, card,
                sport_has_chinese(sport) ? "目标数值" : "TARGET",
                sport->fonts.small, 0x667894,
                28, 13, 132, LV_TEXT_ALIGN_LEFT);
    sport_box(sport, card, 26, 42, 332, 54,
              0xeef3fb, 14, LV_OPA_COVER);
    sport->goal_input_value = sport_label(
        sport, card, "0", sport->fonts.body, 0x142a65,
        38, 54, 218, LV_TEXT_ALIGN_RIGHT);
    sport->goal_editor_unit = sport_label(
        sport, card, "", sport->fonts.small, 0x667894,
        266, 61, 78, LV_TEXT_ALIGN_LEFT);
    sport->goal_input_hint = sport_label(
        sport, card, "", sport->fonts.small, 0x667894,
        28, 101, 328, LV_TEXT_ALIGN_CENTER);

    for (row = 0; row < 3; row++) {
        for (column = 0; column < 3; column++) {
            char number[4];

            snprintf(number, sizeof(number), "%d", digit);
            sport_create_goal_key(sport, card,
                                  30 + column * 116,
                                  132 + row * 58,
                                  digit, number);
            digit++;
        }
    }
    sport_create_goal_key(sport, card, 30, 306, 10,
                          sport_has_chinese(sport) ? "清空" : "CLR");
    sport_create_goal_key(sport, card, 146, 306, 0, "0");
    sport_create_goal_key(sport, card, 262, 306, 11,
                          sport_has_chinese(sport) ? "删除" : "DEL");

    confirm = sport_make_button(sport, card,
                                30, 365, 324, 48,
                                0x347ff0, 0x86b6ff, 2,
                                sport_goal_confirm_clicked);
    sport_label(sport, confirm,
                sport_has_chinese(sport) ? "确认修改" : "SAVE GOAL",
                sport->fonts.body, 0xffffff,
                8, 10, 308, LV_TEXT_ALIGN_CENTER);
    sport_set_hidden(sport->goal_editor_page, true);
}

static void sport_create_celebration_layer(openvela_ui_sport_t *sport)
{
    lv_obj_t *parent = lv_obj_get_parent(sport->root);
    lv_obj_t *card;
    int index;
    static const int16_t dots[12][3] = {
        {42, 76, 13}, {83, 112, 8}, {135, 61, 10},
        {326, 73, 12}, {374, 117, 8}, {298, 106, 7},
        {48, 398, 9}, {92, 438, 13}, {147, 407, 7},
        {326, 416, 10}, {378, 382, 13}, {286, 450, 7},
    };

    sport->celebration_layer = sport_box(sport, parent,
                                         0, 0, 432, 514,
                                         0x07122f, 0, LV_OPA_COVER);
    lv_obj_add_flag(sport->celebration_layer, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(sport->celebration_layer,
                        sport_celebration_clicked,
                        LV_EVENT_CLICKED, sport);
    for (index = 0; index < 12; index++) {
        sport_box(sport, sport->celebration_layer,
                  dots[index][0], dots[index][1],
                  dots[index][2], dots[index][2],
                  index % 3 == 0 ? 0xffd24d :
                  (index % 3 == 1 ? 0xff6f91 : 0x58c9ff),
                  dots[index][2] / 2, LV_OPA_COVER);
    }
    card = sport_box(sport, sport->celebration_layer,
                     38, 145, 356, 218,
                     0x142454, 32, LV_OPA_COVER);
    lv_obj_set_style_border_color(card, lv_color_hex(0xffd24d), 0);
    lv_obj_set_style_border_width(card, sport_sx(sport, 3), 0);
    sport_label(sport, card,
                sport_has_chinese(sport) ? "恭喜！" : "CONGRATULATIONS!",
                sport->fonts.title, 0xffd24d,
                24, 26, 308, LV_TEXT_ALIGN_CENTER);
    sport_label(sport, card,
                sport_has_chinese(sport) ?
                    "今天所有运动目标" : "ALL SPORT GOALS",
                sport->fonts.body, 0xffffff,
                24, 101, 308, LV_TEXT_ALIGN_CENTER);
    sport_label(sport, card,
                sport_has_chinese(sport) ?
                    "已经完成！" : "COMPLETED!",
                sport->fonts.body, 0xffffff,
                24, 151, 308, LV_TEXT_ALIGN_CENTER);
    sport_label(sport, sport->celebration_layer,
                sport_has_chinese(sport) ?
                    "点击屏幕继续" : "TAP TO CONTINUE",
                sport->fonts.small, 0xe6ebf7,
                56, 408, 320, LV_TEXT_ALIGN_CENTER);
    sport_set_hidden(sport->celebration_layer, true);
}

static void sport_create_heart_page(openvela_ui_sport_t *sport)
{
    sport->heart_page = sport_page_create(sport);
    sport_box(sport, sport->heart_page, 0, 0, 432, 514,
              0x02030a, 0, (lv_opa_t)194);
    sport_label(sport, sport->heart_page,
                sport_has_chinese(sport) ? "持续心率" : "HEART RATE",
                sport->fonts.title, 0xffffff,
                0, 24, 432, LV_TEXT_ALIGN_CENTER);

    sport->heart_x = sport_sx(sport, 114);
    sport->heart_y = sport_sx(sport, 86);
    sport->heart_image = sport_image(sport, sport->heart_page,
                                     SPORT_HEART_PATH,
                                     114, 86, 256, 205);
    if (sport->heart_image) {
        sport->heart_visual = sport->heart_image;
        sport->heart_base_scale = (uint32_t)sport_sx(sport, 205);
        lv_obj_set_user_data(sport->heart_image, sport);
    } else {
        sport->heart_visual = sport_make_icon_fallback(
            sport, sport->heart_page, 114, 86, 205, "♥", 0xff232b);
    }

    sport_label(sport, sport->heart_page,
                sport_has_chinese(sport) ?
                    "运动模式 · 长时间监测中" : "WORKOUT MONITORING",
                sport->fonts.small, 0xffb9bd,
                0, 286, 432, LV_TEXT_ALIGN_CENTER);
    sport->heart_value = sport_label(sport, sport->heart_page, "112",
                                     sport->fonts.clock, 0xff3038,
                                     51, 320, 246,
                                     LV_TEXT_ALIGN_RIGHT);
    sport_label(sport, sport->heart_page, "BPM",
                sport->fonts.body, 0xf4e9ea,
                307, 360, 72, LV_TEXT_ALIGN_LEFT);
    sport->heart_threshold = sport_label(sport, sport->heart_page,
        sport_has_chinese(sport) ?
            "超过 160 BPM 将自动提醒" : "ALERT ABOVE 160 BPM",
        sport->fonts.small, 0xe1d6db,
        0, 430, 432, LV_TEXT_ALIGN_CENTER);
    sport_set_hidden(sport->heart_page, true);
}

static void sport_create_exit_page(openvela_ui_sport_t *sport)
{
    lv_obj_t *jump;
    lv_obj_t *button;

    sport->exit_page = sport_page_create(sport);
    sport_box(sport, sport->exit_page, 0, 0, 432, 514,
              0x07143a, 0, (lv_opa_t)107);
    sport_label(sport, sport->exit_page,
                sport_has_chinese(sport) ?
                    "退出运动模式" : "END WORKOUT",
                sport->fonts.body, 0xffffff,
                0, 28, 432, LV_TEXT_ALIGN_CENTER);

    jump = sport_image(sport, sport->exit_page, SPORT_JUMP_ROPE_PATH,
                       170, 87, 144, 92);
    if (!jump) {
        sport_make_icon_fallback(sport, sport->exit_page,
                                 170, 87, 92,
                                 sport_has_chinese(sport) ? "跳" : "GO",
                                 0x6559ef);
    }

    sport_label(sport, sport->exit_page,
                sport_has_chinese(sport) ?
                    "本次运动数据已实时保存" : "WORKOUT DATA SAVED",
                sport->fonts.small, 0xffffff,
                0, 180, 432, LV_TEXT_ALIGN_CENTER);

    sport->exit_cat_y = sport_sx(sport, 220);
    sport->exit_cat_visual = sport_make_cat(sport, sport->exit_page,
                                            133, 220, 166,
                                            &sport->exit_cat_image);

    button = sport_make_button(sport, sport->exit_page,
                               56, 408, 320, 64,
                               0x8d1f39, 0xff7782, 3,
                               sport_exit_clicked);
    sport_label(sport, button,
                sport_has_chinese(sport) ?
                    "退出运动模式" : "END WORKOUT",
                sport->fonts.body, 0xffffff,
                10, 16, 300, LV_TEXT_ALIGN_CENTER);
    sport_set_hidden(sport->exit_page, true);
}

static void sport_create_confirm_layer(openvela_ui_sport_t *sport)
{
    lv_obj_t *card;
    lv_obj_t *cancel;
    lv_obj_t *confirm;

    sport->confirm_layer = sport_box(sport, sport->root,
                                     0, 0, 432, 514,
                                     0x02030a, 0, (lv_opa_t)205);
    lv_obj_add_flag(sport->confirm_layer, LV_OBJ_FLAG_CLICKABLE);

    card = sport_box(sport, sport->confirm_layer,
                     39, 142, 354, 230,
                     0x101b43, 30, LV_OPA_COVER);
    lv_obj_set_style_border_color(card, lv_color_hex(0xff6975), 0);
    lv_obj_set_style_border_width(card, sport_sx(sport, 3), 0);
    sport_label(sport, card,
                sport_has_chinese(sport) ?
                    "结束本次运动？" : "END WORKOUT?",
                sport->fonts.body, 0xffffff,
                20, 28, 314, LV_TEXT_ALIGN_CENTER);
    sport_label(sport, card,
                sport_has_chinese(sport) ?
                    "运动数据已经保存" : "YOUR DATA IS SAVED",
                sport->fonts.small, 0xd8deed,
                20, 88, 314, LV_TEXT_ALIGN_CENTER);

    cancel = sport_make_button(sport, card,
                               20, 150, 144, 56,
                               0x26345f, 0x7890c8, 2,
                               sport_exit_cancel_clicked);
    sport_label(sport, cancel,
                sport_has_chinese(sport) ? "取消" : "CANCEL",
                sport->fonts.body, 0xffffff,
                8, 13, 128, LV_TEXT_ALIGN_CENTER);

    confirm = sport_make_button(sport, card,
                                190, 150, 144, 56,
                                0x9f233c, 0xff7782, 2,
                                sport_exit_confirm_clicked);
    sport_label(sport, confirm,
                sport_has_chinese(sport) ? "确认结束" : "END",
                sport->fonts.body, 0xffffff,
                8, 13, 128, LV_TEXT_ALIGN_CENTER);
    sport_set_hidden(sport->confirm_layer, true);
}

static lv_obj_t *sport_view_object(openvela_ui_sport_t *sport,
                                   openvela_ui_sport_page_t page,
                                   sport_stats_view_t stats_view)
{
    switch (page) {
    case OPENVELA_UI_SPORT_PAGE_ENTRY:
        return sport->entry_page;
    case OPENVELA_UI_SPORT_PAGE_STEPS:
    case OPENVELA_UI_SPORT_PAGE_CALORIES:
    case OPENVELA_UI_SPORT_PAGE_DURATION:
        if (stats_view == SPORT_STATS_HISTORY) {
            return sport->history_page;
        }
        if (stats_view == SPORT_STATS_HISTORY_DETAIL) {
            return sport->stats_page;
        }
        if (stats_view == SPORT_STATS_GOAL) {
            return sport->goal_page;
        }
        if (stats_view == SPORT_STATS_GOAL_EDITOR) {
            return sport->goal_editor_page;
        }
        return sport->stats_page;
    case OPENVELA_UI_SPORT_PAGE_HEART_RATE:
        return sport->heart_page;
    case OPENVELA_UI_SPORT_PAGE_EXIT:
        return sport->exit_page;
    default:
        return NULL;
    }
}

static bool sport_page_valid(openvela_ui_sport_page_t page)
{
    switch (page) {
    case OPENVELA_UI_SPORT_PAGE_ENTRY:
    case OPENVELA_UI_SPORT_PAGE_STEPS:
    case OPENVELA_UI_SPORT_PAGE_CALORIES:
    case OPENVELA_UI_SPORT_PAGE_DURATION:
    case OPENVELA_UI_SPORT_PAGE_HEART_RATE:
    case OPENVELA_UI_SPORT_PAGE_EXIT:
        return true;
    default:
        return false;
    }
}

static uint32_t sport_metric_value(openvela_ui_sport_page_t page,
                                   uint32_t steps)
{
    if (page == OPENVELA_UI_SPORT_PAGE_CALORIES) {
        return (uint32_t)(((uint64_t)steps * 4U + 50U) / 100U);
    }
    if (page == OPENVELA_UI_SPORT_PAGE_DURATION) {
        return steps / 100U;
    }
    return steps;
}

static void sport_format_number(char *buffer, size_t size, uint32_t value)
{
    char plain[16];
    size_t input;
    size_t output = 0;
    size_t index;

    snprintf(plain, sizeof(plain), "%lu", (unsigned long)value);
    input = strlen(plain);
    for (index = 0; index < input && output + 1 < size; index++) {
        if (index > 0 && (input - index) % 3 == 0 && output + 2 < size) {
            buffer[output++] = ',';
        }
        buffer[output++] = plain[index];
    }
    buffer[output] = '\0';
}

static void sport_format_compact(char *buffer, size_t size, uint32_t value)
{
    uint64_t tenths;

    if (value < 10000U) {
        sport_format_number(buffer, size, value);
        return;
    }
    tenths = ((uint64_t)value + 500U) / 1000U;
    snprintf(buffer, size, "%lu.%lu万",
             (unsigned long)(tenths / 10U),
             (unsigned long)(tenths % 10U));
}

static void sport_hide_chart_bubble(openvela_ui_sport_t *sport)
{
    if (sport->chart_bubble_timer) {
        lv_timer_pause(sport->chart_bubble_timer);
    }
    sport_set_hidden(sport->chart_bubble, true);
    sport->chart_bubble_hour = -1;
}

static void sport_scroll_chart_to_hour(openvela_ui_sport_t *sport, int hour)
{
    int target;
    int maximum = SPORT_CHART_CONTENT_WIDTH - SPORT_CHART_VIEW_WIDTH;

    if (!sport->chart_scroll || !lv_obj_is_valid(sport->chart_scroll)) {
        return;
    }
    if (hour < 0) hour = 0;
    if (hour > 24) hour = 24;
    target = hour * SPORT_CHART_HOUR_WIDTH - 270;
    if (target < 0) target = 0;
    if (target > maximum) target = maximum;
    lv_obj_update_layout(sport->chart_scroll);
    lv_obj_scroll_to_x(sport->chart_scroll, sport_sx(sport, target),
                       LV_ANIM_OFF);
}

static uint32_t sport_chart_maximum(uint32_t highest)
{
    uint64_t rounded;

    if (highest <= 100U) return 100U;
    if (highest <= 500U) return 500U;
    if (highest <= 1000U) return 1000U;
    if (highest <= 2000U) return 2000U;
    if (highest <= 5000U) return 5000U;
    if (highest <= 10000U) return 10000U;
    if (highest <= 20000U) return 20000U;
    if (highest <= 50000U) return 50000U;
    rounded = (((uint64_t)highest + 49999U) / 50000U) * 50000U;
    if (rounded > UINT32_MAX) {
        return UINT32_MAX;
    }
    return rounded > 0U ? (uint32_t)rounded : 1U;
}

static int32_t sport_chart_point_y(openvela_ui_sport_t *sport,
                                   uint32_t value, uint32_t maximum)
{
    if (maximum == 0U) maximum = 1U;
    return sport_sx(sport, SPORT_CHART_BASELINE_Y - (int32_t)(
        ((uint64_t)value * SPORT_CHART_RANGE_Y) / maximum));
}

static void sport_render_chart(openvela_ui_sport_t *sport,
                               const uint32_t boundaries[SPORT_CHART_POINTS],
                               bool history, uint32_t goal)
{
    uint32_t highest = 0U;
    uint32_t maximum;
    uint32_t value;
    uint32_t live_position_milli = 0U;
    uint8_t point_count = 0U;
    uint8_t dot_count;
    uint8_t clickable_hour;
    int index;

    if (!sport->chart_line || !lv_obj_is_valid(sport->chart_line)) {
        return;
    }

    if (history) {
        point_count = SPORT_CHART_POINTS;
        dot_count = SPORT_CHART_POINTS;
        clickable_hour = 24U;
        for (index = 0; index < SPORT_CHART_POINTS; index++) {
            value = sport_metric_value(sport->page, boundaries[index]);
            if (value > highest) highest = value;
        }
        if (goal > highest) highest = goal;
    } else {
        time_t now = time(NULL);
        struct tm local;
        uint32_t seconds = 0U;

        clickable_hour = sport->active_hour;
        dot_count = (uint8_t)(clickable_hour + 1U);
        if (localtime_r(&now, &local) && local.tm_hour == sport->active_hour) {
            seconds = (uint32_t)local.tm_min * 60U +
                      (uint32_t)local.tm_sec;
        }
        live_position_milli = (uint32_t)sport->active_hour * 1000U +
                              seconds * 1000U / 3600U;
        for (index = 0; index <= clickable_hour; index++) {
            value = sport_metric_value(sport->page, boundaries[index]);
            if (value > highest) highest = value;
        }
        value = sport_metric_value(sport->page, sport->steps);
        if (value > highest) highest = value;
        point_count = (uint8_t)(dot_count + 1U);
    }
    maximum = sport_chart_maximum(highest);
    sport->chart_maximum = maximum;
    sport->chart_clickable_hour = clickable_hour;
    sport->chart_dot_count = dot_count;

    memset(sport->chart_hour_values, 0,
           sizeof(sport->chart_hour_values));
    for (index = 0; index <= clickable_hour; index++) {
        uint32_t position_milli = (uint32_t)index * 1000U;
        int32_t x = sport_sx(sport, SPORT_CHART_POINT_X +
            (int32_t)((position_milli * SPORT_CHART_HOUR_WIDTH) / 1000U));
        int32_t y;

        value = sport_metric_value(sport->page, boundaries[index]);
        sport->chart_hour_values[index] = value;
        y = sport_chart_point_y(sport, value, maximum);
        sport->chart_points[index].x = x;
        sport->chart_points[index].y = y;
        lv_obj_set_pos(sport->chart_dots[index],
                       x - sport_sx(sport, 4),
                       y - sport_sx(sport, 4));
        sport_set_hidden(sport->chart_dots[index], false);
    }

    if (!history) {
        int live_index = dot_count;
        int32_t x = sport_sx(sport, SPORT_CHART_POINT_X +
            (int32_t)((live_position_milli * SPORT_CHART_HOUR_WIDTH) /
                      1000U));

        value = sport_metric_value(sport->page, sport->steps);
        sport->chart_points[live_index].x = x;
        sport->chart_points[live_index].y =
            sport_chart_point_y(sport, value, maximum);
    }
    for (index = dot_count; index < SPORT_CHART_POINTS; index++) {
        sport_set_hidden(sport->chart_dots[index], true);
    }

    if (history) {
        int32_t goal_y = sport_chart_point_y(sport, goal, maximum);

        lv_obj_set_y(sport->chart_goal_line, goal_y - sport_sx(sport, 2));
        sport_set_hidden(sport->chart_goal_line, false);
    } else {
        sport_set_hidden(sport->chart_goal_line, true);
    }
    lv_line_set_points(sport->chart_line, sport->chart_points, point_count);
    lv_obj_invalidate(sport->chart_line);
}

static void sport_update_stats(openvela_ui_sport_t *sport)
{
    const char *title;
    const char *current;
    const char *unit;
    char value[24];

    if (sport->page == OPENVELA_UI_SPORT_PAGE_CALORIES) {
        title = sport_has_chinese(sport) ? "卡路里统计" : "CALORIES";
        current = sport_has_chinese(sport) ? "当前卡路里" : "CURRENT";
        unit = sport_has_chinese(sport) ? "千卡" : "KCAL";
    } else if (sport->page == OPENVELA_UI_SPORT_PAGE_DURATION) {
        title = sport_has_chinese(sport) ? "运动时长统计" : "DURATION";
        current = sport_has_chinese(sport) ? "当前运动时长" : "CURRENT";
        unit = sport_has_chinese(sport) ? "分钟" : "MIN";
    } else {
        title = sport_has_chinese(sport) ? "步数统计" : "STEPS";
        current = sport_has_chinese(sport) ? "当前步数" : "CURRENT";
        unit = sport_has_chinese(sport) ? "步" : "STEPS";
    }

    lv_label_set_text(sport->stats_title, title);
    lv_label_set_text(sport->chart_heading,
                      sport_has_chinese(sport) ? "今日趋势" : "TODAY");
    lv_label_set_text(sport->chart_goal_text, "");
    sport_set_hidden(sport->chart_goal_text, true);
    lv_label_set_text(sport->stats_current_label, current);
    lv_label_set_text(sport->stats_unit, unit);
    sport_format_number(value, sizeof(value),
                        sport_metric_value(sport->page, sport->steps));
    lv_obj_set_style_text_font(
        sport->stats_value,
        sport_font(sport_metric_value(sport->page, sport->steps) >= 10000U ?
                       sport->fonts.title : sport->fonts.clock), 0);
    lv_label_set_text(sport->stats_value, value);
    sport_render_chart(sport, sport->hour_steps, false, 0U);
}

static uint32_t sport_history_steps(openvela_ui_sport_t *sport, int index)
{
    sport_history_record_t *record;

    if (index <= 0) {
        return sport->steps;
    }
    if (index >= SPORT_HISTORY_COUNT) {
        index = SPORT_HISTORY_COUNT - 1;
    }
    record = sport_history_for_offset(sport, index);
    return record ? record->steps :
                    g_sport_demo_history_steps[index - 1];
}

static uint32_t sport_history_goal(openvela_ui_sport_t *sport, int index)
{
    int metric = sport_metric_index(sport->page);
    sport_history_record_t *record;

    if (index <= 0) {
        return sport->goals[metric];
    }
    if (index >= SPORT_HISTORY_COUNT) {
        index = SPORT_HISTORY_COUNT - 1;
    }
    record = sport_history_for_offset(sport, index);
    return record ? record->goals[metric] :
                    g_sport_demo_history_goals[metric][index - 1];
}

static void sport_update_history(openvela_ui_sport_t *sport)
{
    char title[40];
    const char *unit = sport_metric_unit(sport->page,
                                         sport_has_chinese(sport));
    int index;

    snprintf(title, sizeof(title), "%s%s",
             sport_metric_name(sport->page, sport_has_chinese(sport)),
             sport_has_chinese(sport) ? "历史" : " HISTORY");
    lv_label_set_text(sport->history_title, title);

    for (index = 0; index < SPORT_HISTORY_COUNT; index++) {
        char date[24];
        char value[24];
        uint32_t metric_value = sport_metric_value(
            sport->page, sport_history_steps(sport, index));

        sport_date_label(index, date, sizeof(date));
        sport_format_number(value, sizeof(value), metric_value);
        lv_label_set_text(sport->history_date_labels[index], date);
        lv_label_set_text(sport->history_value_labels[index], value);
        lv_label_set_text(sport->history_unit_labels[index], unit);
        lv_obj_set_style_bg_opa(sport->history_dots[index],
                                index == 0 ? LV_OPA_COVER : LV_OPA_TRANSP,
                                0);
    }
}

static void sport_update_history_detail(openvela_ui_sport_t *sport)
{
    sport_history_record_t *record;
    uint32_t boundaries[SPORT_CHART_POINTS] = {0U};
    char date[24];
    char value[24];
    char goal_value[24];
    char goal_text[64];
    char current_text[40];
    uint32_t total;
    uint32_t goal;
    uint32_t completed;
    int index;

    if (sport->selected_history == 0 ||
        sport->selected_history >= SPORT_HISTORY_COUNT) {
        sport->selected_history = 1;
    }
    sport_date_label(sport->selected_history, date, sizeof(date));
    total = sport_history_steps(sport, sport->selected_history);
    completed = sport_metric_value(sport->page, total);
    goal = sport_history_goal(sport, sport->selected_history);
    record = sport_history_for_offset(sport, sport->selected_history);
    for (index = 0; index < SPORT_HOURLY_SAMPLES; index++) {
        uint32_t sample;

        if (record && record->samples_valid) {
            sample = record->samples[index];
        } else {
            sample = (uint32_t)(((uint64_t)total *
                                g_sport_history_progress[index]) / 100U);
        }
        if (sample > total) sample = total;
        if (sample < boundaries[index]) sample = boundaries[index];
        boundaries[index + 1] = sample;
    }
    boundaries[SPORT_CHART_POINTS - 1] = total;
    sport_format_number(value, sizeof(value), completed);
    sport_format_number(goal_value, sizeof(goal_value), goal);
    snprintf(goal_text, sizeof(goal_text),
             sport_has_chinese(sport) ? "目标：%s%s" : "GOAL: %s %s",
             goal_value,
             sport_metric_unit(sport->page, sport_has_chinese(sport)));
    snprintf(current_text, sizeof(current_text),
             sport_has_chinese(sport) ? "当日%s" : "DAY %s",
             sport_metric_name(sport->page, sport_has_chinese(sport)));

    lv_label_set_text(sport->stats_title, date);
    lv_label_set_text(sport->chart_heading,
                      sport_has_chinese(sport) ? "当日趋势" : "DAY TREND");
    lv_label_set_text(sport->chart_goal_text, goal_text);
    sport_set_hidden(sport->chart_goal_text, false);
    lv_label_set_text(sport->stats_current_label, current_text);
    lv_label_set_text(sport->stats_value, value);
    lv_obj_set_style_text_font(
        sport->stats_value,
        sport_font(completed >= 10000U ? sport->fonts.title :
                                        sport->fonts.clock), 0);
    lv_label_set_text(sport->stats_unit,
                      sport_metric_unit(sport->page,
                                        sport_has_chinese(sport)));
    sport_render_chart(sport, boundaries, true, goal);
    sport_scroll_chart_to_hour(sport, 24);
}

static void sport_update_goal(openvela_ui_sport_t *sport)
{
    char title[40];
    char goal_value[24];
    char completed_number[24];
    char completed_value[64];
    char percent_value[16];
    char edit_text[48];
    int metric = sport_metric_index(sport->page);
    uint32_t completed = sport_metric_value(sport->page, sport->steps);
    uint32_t goal = sport->goals[metric];
    uint32_t percent = goal > 0 ?
        (uint32_t)(((uint64_t)completed * 100U) / goal) : 0U;

    if (percent > 100U) percent = 100U;
    snprintf(title, sizeof(title), "%s%s",
             sport_metric_name(sport->page, sport_has_chinese(sport)),
             sport_has_chinese(sport) ? "目标" : " GOAL");
    sport_format_number(goal_value, sizeof(goal_value), goal);
    sport_format_number(completed_number, sizeof(completed_number), completed);
    snprintf(completed_value, sizeof(completed_value),
             sport_has_chinese(sport) ? "已完成 %s %s" :
                                        "COMPLETED %s %s",
             completed_number,
             sport_metric_unit(sport->page, sport_has_chinese(sport)));
    snprintf(percent_value, sizeof(percent_value), "%lu%%",
             (unsigned long)percent);
    if (sport_has_chinese(sport)) {
        if (sport->page == OPENVELA_UI_SPORT_PAGE_STEPS) {
            snprintf(edit_text, sizeof(edit_text), "修改目标步数");
        } else {
            snprintf(edit_text, sizeof(edit_text), "修改%s目标",
                     sport_metric_name(sport->page, true));
        }
    } else {
        snprintf(edit_text, sizeof(edit_text), "EDIT %s GOAL",
                 sport_metric_name(sport->page, false));
    }

    lv_label_set_text(sport->goal_title, title);
    lv_label_set_text(sport->goal_value, goal_value);
    lv_label_set_text(sport->goal_unit,
                      sport_metric_unit(sport->page,
                                        sport_has_chinese(sport)));
    lv_label_set_text(sport->goal_completed, completed_value);
    lv_label_set_text(sport->goal_percent, percent_value);
    lv_label_set_text(sport->goal_edit_label, edit_text);
    lv_arc_set_value(sport->goal_arc, (int32_t)percent);
}

static void sport_update_goal_editor(openvela_ui_sport_t *sport)
{
    char value[24];
    char hint[72];
    int metric = sport_metric_index(sport->page);

    sport_format_number(value, sizeof(value), sport->goal_input);
    snprintf(hint, sizeof(hint),
             sport_has_chinese(sport) ? "请输入 %lu～%lu" :
                                        "ENTER %lu - %lu",
             (unsigned long)sport_goal_minimum(metric),
             (unsigned long)sport_goal_maximum(metric));
    lv_label_set_text(sport->goal_input_value, value);
    lv_label_set_text(sport->goal_editor_unit,
                      sport_metric_unit(sport->page,
                                        sport_has_chinese(sport)));
    lv_label_set_text(sport->goal_input_hint, hint);
    lv_obj_set_style_text_color(sport->goal_input_hint,
                                lv_color_hex(0x667894), 0);
}

static void sport_update_heart(openvela_ui_sport_t *sport)
{
    if (sport->heart_value && lv_obj_is_valid(sport->heart_value)) {
        lv_label_set_text_fmt(sport->heart_value, "%u",
                              (unsigned int)sport->heart_rate);
    }
}

static void sport_seed_hours(openvela_ui_sport_t *sport)
{
    time_t now = time(NULL);
    struct tm local;
    int hour;
    int boundary;
    uint32_t denominator;
    uint32_t previous = 0U;

    localtime_r(&now, &local);
    hour = local.tm_hour;
    if (hour < 0 || hour > 23) hour = 12;
    sport->active_hour = (uint8_t)hour;
    sport->hour_point_count = (uint8_t)(hour + 2);
    denominator = g_sport_history_progress[hour];

    memset(sport->hour_steps, 0, sizeof(sport->hour_steps));
    sport->hour_steps[0] = 0U;
    if (sport->day_samples_valid) {
        for (boundary = 1; boundary <= hour + 1; boundary++) {
            uint32_t value = sport->day_samples[boundary - 1];

            if (value > sport->steps) value = sport->steps;
            if (value < previous) value = previous;
            sport->hour_steps[boundary] = value;
            previous = value;
        }
    } else {
        for (boundary = 1; boundary <= hour; boundary++) {
            uint32_t progress = g_sport_history_progress[boundary - 1];

            if (denominator > 0U) {
                sport->hour_steps[boundary] = (uint32_t)(
                    ((uint64_t)sport->steps * progress) / denominator);
            } else {
                sport->hour_steps[boundary] = (uint32_t)(
                    ((uint64_t)sport->steps * (uint32_t)boundary) /
                    (uint32_t)(hour + 1));
            }
        }
    }
    sport->hour_steps[hour + 1] = sport->steps;
    sport_sync_day_samples(sport);
}

static void sport_update_hour_total(openvela_ui_sport_t *sport)
{
    time_t now = time(NULL);
    struct tm local;
    int hour;
    int boundary;

    localtime_r(&now, &local);
    hour = local.tm_hour;
    if (hour < 0 || hour > 23) hour = sport->active_hour;

    if (hour < sport->active_hour) {
        sport_seed_hours(sport);
        return;
    }
    if (hour > sport->active_hour) {
        uint32_t previous = sport->hour_steps[sport->active_hour + 1];
        for (boundary = sport->active_hour + 2;
             boundary <= hour + 1 && boundary < SPORT_CHART_POINTS;
             boundary++) {
            sport->hour_steps[boundary] = previous;
        }
        sport->active_hour = (uint8_t)hour;
        sport->hour_point_count = (uint8_t)(hour + 2);
    }
    sport->hour_steps[hour + 1] = sport->steps;
    sport_sync_day_samples(sport);
}

static uint32_t sport_random(openvela_ui_sport_t *sport)
{
    sport->random_state = sport->random_state * 1664525U + 1013904223U;
    return sport->random_state;
}

static void sport_data_tick(lv_timer_t *timer)
{
    openvela_ui_sport_t *sport = lv_timer_get_user_data(timer);
    uint32_t now;
    bool stats_changed = false;
    bool heart_changed = false;
    bool protect_stored_day = false;
    bool clock_valid;
    char today[9];

    if (!sport || sport->destroying) {
        return;
    }
    if (!sport->visible && !sport->special_active) {
        return;
    }

    now = lv_tick_get();
    clock_valid = sport_today_key(today);
    if (!clock_valid && sport_day_key_valid(sport->data_day)) {
        protect_stored_day = true;
    } else if (clock_valid &&
               strcmp(today, sport->data_day) != 0) {
        if (sport_day_key_valid(sport->data_day) &&
            strcmp(today, sport->data_day) < 0) {
            /* The board starts at 1970 and may briefly pass through another
             * stale time while NTP settles.  Never overwrite a persisted day
             * with data from a clock that is still behind it. */
            protect_stored_day = true;
        } else {
            sport_history_capture_current(sport);
            snprintf(sport->data_day, sizeof(sport->data_day), "%s", today);
            sport->steps = 0U;
            sport->external_steps = false;
            memset(sport->day_samples, 0, sizeof(sport->day_samples));
            sport->day_samples_valid = false;
            sport_seed_hours(sport);
            sport->persist_dirty = true;
            sport_save_state(sport);
            stats_changed = true;
        }
    }
    if (!protect_stored_day && !sport->external_steps &&
        lv_tick_elaps(sport->last_step_tick) >= SPORT_STEP_PERIOD_MS) {
        uint32_t increment = 2U + sport_random(sport) % 4U;

        sport->last_step_tick = now;
        if (sport->steps < SPORT_MAX_STEPS) {
            if (increment > SPORT_MAX_STEPS - sport->steps) {
                sport->steps = SPORT_MAX_STEPS;
            } else {
                sport->steps += increment;
            }
            sport_update_hour_total(sport);
            sport->persist_dirty = true;
            stats_changed = true;
        }
    }

    if (sport->special_active && !sport->external_heart &&
        lv_tick_elaps(sport->last_heart_tick) >= SPORT_HEART_PERIOD_MS) {
        int rate = sport->heart_rate;
        int delta = (int)(sport_random(sport) % 8U) - 3;

        sport->last_heart_tick = now;
        if (rate < 100) delta += 2;
        if (rate > 150) delta -= 2;
        rate += delta;
        if (rate < 88) rate = 88;
        if (rate > 168) rate = 168;
        sport->heart_rate = (uint16_t)rate;
        heart_changed = true;
    }

    if (!sport->low_power) {
        if (stats_changed && sport->stats_view == SPORT_STATS_TODAY &&
            sport->page >= OPENVELA_UI_SPORT_PAGE_STEPS &&
            sport->page <= OPENVELA_UI_SPORT_PAGE_DURATION) {
            sport_update_stats(sport);
        } else if (stats_changed &&
                   sport->stats_view == SPORT_STATS_HISTORY) {
            sport_update_history(sport);
        } else if (stats_changed &&
                   sport->stats_view == SPORT_STATS_GOAL) {
            sport_update_goal(sport);
        }
        if (heart_changed) {
            sport_update_heart(sport);
        }
    }
    if (sport->visible && !sport->low_power && !sport->transitioning &&
        !sport->confirm_visible &&
        lv_tick_elaps(sport->last_motion_tick) >=
            SPORT_MOTION_PERIOD_MS) {
        sport->last_motion_tick = now;
        sport->motion_phase = !sport->motion_phase;
        sport_apply_motion_frame(sport);
    }
    if (stats_changed && !sport->low_power) {
        sport_check_celebration(sport);
    }
}

static void sport_apply_motion_frame(openvela_ui_sport_t *sport)
{
    int32_t offset = sport_sx(sport, sport->motion_phase ? 2 : -2);

    if (sport->page == OPENVELA_UI_SPORT_PAGE_ENTRY &&
        sport->entry_cat_visual &&
        lv_obj_is_valid(sport->entry_cat_visual)) {
        lv_obj_set_y(sport->entry_cat_visual,
                     sport->entry_cat_y + offset);
    } else if (sport->page == OPENVELA_UI_SPORT_PAGE_EXIT &&
               sport->exit_cat_visual &&
               lv_obj_is_valid(sport->exit_cat_visual)) {
        lv_obj_set_y(sport->exit_cat_visual,
                     sport->exit_cat_y + offset);
    } else if (sport->page == OPENVELA_UI_SPORT_PAGE_HEART_RATE) {
        if (sport->heart_image && lv_obj_is_valid(sport->heart_image)) {
            uint32_t scale = sport->heart_base_scale *
                (sport->motion_phase ? 105U : 96U) / 100U;

            lv_image_set_scale(sport->heart_image, scale);
            lv_obj_set_pos(sport->heart_image,
                sport->heart_x +
                    ((int32_t)sport->heart_base_scale - (int32_t)scale) / 2,
                sport->heart_y +
                    ((int32_t)sport->heart_base_scale - (int32_t)scale) / 2);
        } else if (sport->heart_visual &&
                   lv_obj_is_valid(sport->heart_visual)) {
            lv_obj_set_y(sport->heart_visual,
                         sport->heart_y + offset);
        }
    }
}

static void sport_start_page_animation(openvela_ui_sport_t *sport)
{
    if (!sport->visible || sport->low_power || sport->transitioning ||
        sport->confirm_visible || sport->celebration_visible) {
        return;
    }

    /* Discrete two-frame motion keeps the reference's floating/pulse cue at
     * 2 FPS.  A normal LVGL infinite animation would redraw at the 16 ms
     * display period, and this board rotates the complete framebuffer for
     * every one of those nominally local updates. */
    sport->motion_phase = false;
    sport->last_motion_tick = lv_tick_get();
    sport_apply_motion_frame(sport);
}

static void sport_stop_page_animations(openvela_ui_sport_t *sport)
{
    if (sport->entry_cat_visual &&
        lv_obj_is_valid(sport->entry_cat_visual)) {
        lv_anim_delete(sport->entry_cat_visual, NULL);
        lv_obj_set_y(sport->entry_cat_visual, sport->entry_cat_y);
    }
    if (sport->exit_cat_visual && lv_obj_is_valid(sport->exit_cat_visual)) {
        lv_anim_delete(sport->exit_cat_visual, NULL);
        lv_obj_set_y(sport->exit_cat_visual, sport->exit_cat_y);
    }
    if (sport->heart_image && lv_obj_is_valid(sport->heart_image)) {
        lv_anim_delete(sport->heart_image, NULL);
        lv_image_set_scale(sport->heart_image, sport->heart_base_scale);
        lv_obj_set_pos(sport->heart_image, sport->heart_x, sport->heart_y);
    } else if (sport->heart_visual &&
               lv_obj_is_valid(sport->heart_visual)) {
        lv_anim_delete(sport->heart_visual, NULL);
        lv_obj_set_y(sport->heart_visual, sport->heart_y);
    }
}

static void sport_emit_event(openvela_ui_sport_t *sport,
                             openvela_ui_sport_event_t event)
{
    openvela_ui_sport_event_cb_t callback = sport->event_cb;
    void *user_data = sport->event_user_data;

    if (callback) {
        callback(sport, event, user_data);
    }
}

static void sport_transition_begin_async(void *data)
{
    openvela_ui_sport_t *sport = data;
    lv_obj_t *old_page;
    lv_obj_t *new_page;
    openvela_ui_sport_event_t event;
    bool emit;

    if (!sport || sport->destroying || !sport->transitioning) {
        return;
    }

    sport_stop_page_animations(sport);
    sport_hide_chart_bubble(sport);
    old_page = sport_view_object(sport, sport->page, sport->stats_view);
    new_page = sport_view_object(sport, sport->pending_page,
                                 sport->pending_stats_view);
    if (old_page != new_page) {
        sport_set_hidden(old_page, true);
        sport_set_hidden(new_page, false);
    }
    sport->page = sport->pending_page;
    sport->stats_view = sport->pending_stats_view;
    if (sport->stats_view == SPORT_STATS_TODAY &&
        sport->page >= OPENVELA_UI_SPORT_PAGE_STEPS &&
        sport->page <= OPENVELA_UI_SPORT_PAGE_DURATION) {
        sport_update_stats(sport);
        sport_scroll_chart_to_hour(sport, sport->active_hour);
    } else if (sport->stats_view == SPORT_STATS_HISTORY) {
        sport_update_history(sport);
    } else if (sport->stats_view == SPORT_STATS_HISTORY_DETAIL) {
        sport_update_history_detail(sport);
    } else if (sport->stats_view == SPORT_STATS_GOAL) {
        sport_update_goal(sport);
    } else if (sport->stats_view == SPORT_STATS_GOAL_EDITOR) {
        sport_update_goal_editor(sport);
    } else if (sport->page == OPENVELA_UI_SPORT_PAGE_HEART_RATE) {
        sport_update_heart(sport);
    }
    sport->transitioning = false;
    sport_start_page_animation(sport);

    emit = sport->pending_event_valid;
    event = sport->pending_event;
    sport->pending_event_valid = false;
    if (emit) {
        /* The owner may hide or destroy the module in this callback. */
        sport_emit_event(sport, event);
    }
}

static bool sport_queue_page(openvela_ui_sport_t *sport,
                             openvela_ui_sport_page_t page,
                             int direction,
                             bool emit_event,
                             openvela_ui_sport_event_t event)
{
    if (!sport || sport->destroying || sport->transitioning ||
        !sport_page_valid(page) ||
        (page == sport->page && sport->stats_view == SPORT_STATS_TODAY)) {
        return false;
    }

    sport->pending_page = page;
    sport->pending_stats_view = SPORT_STATS_TODAY;
    sport->transition_direction = direction < 0 ? -1 : 1;
    sport->pending_event = event;
    sport->pending_event_valid = emit_event;
    sport->transitioning = true;
    if (lv_async_call(sport_transition_begin_async, sport) != LV_RESULT_OK) {
        sport->transitioning = false;
        sport->pending_event_valid = false;
        return false;
    }
    return true;
}

static bool sport_queue_stats_view(openvela_ui_sport_t *sport,
                                   sport_stats_view_t view, int direction)
{
    if (!sport || sport->destroying || sport->transitioning ||
        sport->special_active ||
        sport->page < OPENVELA_UI_SPORT_PAGE_STEPS ||
        sport->page > OPENVELA_UI_SPORT_PAGE_DURATION ||
        view < SPORT_STATS_TODAY || view > SPORT_STATS_GOAL_EDITOR ||
        view == sport->stats_view) {
        return false;
    }

    sport->pending_page = sport->page;
    sport->pending_stats_view = view;
    sport->transition_direction = direction < 0 ? -1 : 1;
    sport->pending_event_valid = false;
    sport->transitioning = true;
    if (lv_async_call(sport_transition_begin_async, sport) != LV_RESULT_OK) {
        sport->transitioning = false;
        return false;
    }
    return true;
}

static void sport_event_async(void *data)
{
    openvela_ui_sport_t *sport = data;
    openvela_ui_sport_event_t event;

    if (!sport || sport->destroying || !sport->pending_event_valid) {
        return;
    }
    event = sport->pending_event;
    sport->pending_event_valid = false;
    sport->transitioning = false;
    sport_emit_event(sport, event);
}

static bool sport_queue_event(openvela_ui_sport_t *sport,
                              openvela_ui_sport_event_t event)
{
    if (!sport || sport->destroying || sport->transitioning) {
        return false;
    }
    sport->pending_event = event;
    sport->pending_event_valid = true;
    sport->transitioning = true;
    if (lv_async_call(sport_event_async, sport) != LV_RESULT_OK) {
        sport->pending_event_valid = false;
        sport->transitioning = false;
        return false;
    }
    return true;
}

static void sport_show_celebration(openvela_ui_sport_t *sport)
{
    char today[9];

    if (!sport || sport->destroying || sport->celebration_visible ||
        !sport->celebration_layer ||
        !lv_obj_is_valid(sport->celebration_layer)) {
        return;
    }
    if (!sport_today_key(today)) {
        sport->celebration_pending = true;
        return;
    }
    if (sport_day_key_valid(sport->data_day) &&
        strcmp(today, sport->data_day) != 0) {
        sport->celebration_pending = true;
        return;
    }
    sport->celebration_pending = false;
    sport->celebration_visible = true;
    snprintf(sport->last_celebration_day,
             sizeof(sport->last_celebration_day), "%s", today);
    sport->persist_dirty = true;
    sport_save_state(sport);
    sport_stop_page_animations(sport);
    sport_set_hidden(sport->celebration_layer, false);
    lv_obj_move_foreground(sport->celebration_layer);
}

static void sport_check_celebration(openvela_ui_sport_t *sport)
{
    char today[9];
    uint32_t calories;
    uint32_t duration;

    if (!sport || sport->destroying || sport->celebration_visible) {
        return;
    }
    if (!sport_today_key(today)) {
        sport->celebration_pending = true;
        return;
    }
    if (sport_day_key_valid(sport->data_day) &&
        strcmp(today, sport->data_day) != 0) {
        sport->celebration_pending = true;
        return;
    }
    if (strcmp(today, sport->last_celebration_day) == 0) {
        sport->celebration_pending = false;
        return;
    }

    calories = sport_metric_value(OPENVELA_UI_SPORT_PAGE_CALORIES,
                                  sport->steps);
    duration = sport_metric_value(OPENVELA_UI_SPORT_PAGE_DURATION,
                                  sport->steps);
    if (sport->steps < sport->goals[0] ||
        calories < sport->goals[1] ||
        duration < sport->goals[2]) {
        sport->celebration_pending = false;
        return;
    }

    if (!sport->visible && !sport->special_active) {
        sport->celebration_pending = true;
        return;
    }
    sport_show_celebration(sport);
}

static void sport_chart_bubble_timer_cb(lv_timer_t *timer)
{
    openvela_ui_sport_t *sport = lv_timer_get_user_data(timer);

    if (!sport || sport->destroying) {
        return;
    }
    sport_set_hidden(sport->chart_bubble, true);
    sport->chart_bubble_hour = -1;
    lv_timer_pause(timer);
}

static void sport_chart_scroll_event(lv_event_t *event)
{
    openvela_ui_sport_t *sport = lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);

    if (!sport || sport->destroying) {
        return;
    }
    if (code == LV_EVENT_PRESSED || code == LV_EVENT_SCROLL_BEGIN ||
        code == LV_EVENT_SCROLL) {
        sport->chart_touching = true;
        sport->chart_touch_seen = true;
        sport->last_chart_touch_tick = lv_tick_get();
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST ||
               code == LV_EVENT_SCROLL_END) {
        sport->chart_touching = false;
        sport->chart_touch_seen = true;
        sport->last_chart_touch_tick = lv_tick_get();
    }
}

static void sport_chart_hour_clicked(lv_event_t *event)
{
    openvela_ui_sport_t *sport = lv_event_get_user_data(event);
    lv_obj_t *target = lv_event_get_current_target(event);
    intptr_t hour = target ? (intptr_t)lv_obj_get_user_data(target) : -1;
    const char *unit;
    char compact[32];
    char text[72];
    uint32_t value;
    int point_y;
    int left;
    int top;

    if (!sport || sport->destroying || !sport->visible ||
        sport->transitioning || sport->celebration_visible ||
        (sport->stats_view != SPORT_STATS_TODAY &&
         sport->stats_view != SPORT_STATS_HISTORY_DETAIL) ||
        hour < 0 || hour > sport->chart_clickable_hour || hour > 24) {
        return;
    }

    value = sport->chart_hour_values[hour];
    sport_format_compact(compact, sizeof(compact), value);
    unit = sport_metric_unit(sport->page, sport_has_chinese(sport));
    snprintf(text, sizeof(text), "%02ld:00  %s%s",
             (long)hour, compact, unit);
    point_y = SPORT_CHART_BASELINE_Y - (int)(
        ((uint64_t)value * SPORT_CHART_RANGE_Y) /
        (sport->chart_maximum > 0U ? sport->chart_maximum : 1U));
    left = (int)hour * SPORT_CHART_HOUR_WIDTH +
           SPORT_CHART_POINT_X - 66;
    if (left < 0) left = 0;
    if (left > SPORT_CHART_CONTENT_WIDTH - 132) {
        left = SPORT_CHART_CONTENT_WIDTH - 132;
    }
    top = point_y - 39;
    if (top < 0) top = 0;
    if (top > 100) top = 100;

    lv_label_set_text(sport->chart_bubble_label, text);
    lv_obj_set_pos(sport->chart_bubble, sport_sx(sport, left),
                   sport_sx(sport, top));
    sport->chart_bubble_hour = (int8_t)hour;
    sport_set_hidden(sport->chart_bubble, false);
    lv_obj_move_foreground(sport->chart_bubble);
    if (sport->chart_bubble_timer) {
        lv_timer_set_period(sport->chart_bubble_timer,
                            SPORT_BUBBLE_HIDE_MS);
        lv_timer_reset(sport->chart_bubble_timer);
        lv_timer_resume(sport->chart_bubble_timer);
    }
}

static void sport_history_row_clicked(lv_event_t *event)
{
    openvela_ui_sport_t *sport = lv_event_get_user_data(event);
    lv_obj_t *row = lv_event_get_current_target(event);
    intptr_t index = row ? (intptr_t)lv_obj_get_user_data(row) : -1;

    if (!sport || sport->destroying || !sport->visible ||
        sport->special_active || sport->transitioning ||
        sport->celebration_visible ||
        sport->stats_view != SPORT_STATS_HISTORY ||
        index <= 0 || index >= SPORT_HISTORY_COUNT) {
        return;
    }
    sport->selected_history = (uint8_t)index;
    sport_queue_stats_view(sport, SPORT_STATS_HISTORY_DETAIL, 1);
}

static void sport_goal_edit_clicked(lv_event_t *event)
{
    openvela_ui_sport_t *sport = lv_event_get_user_data(event);
    int metric;

    if (!sport || sport->destroying || !sport->visible ||
        sport->special_active || sport->transitioning ||
        sport->celebration_visible ||
        sport->stats_view != SPORT_STATS_GOAL) {
        return;
    }
    metric = sport_metric_index(sport->page);
    sport->goal_input = sport->goals[metric];
    sport->goal_input_fresh = true;
    sport_queue_stats_view(sport, SPORT_STATS_GOAL_EDITOR, 1);
}

static void sport_goal_key_clicked(lv_event_t *event)
{
    openvela_ui_sport_t *sport = lv_event_get_user_data(event);
    lv_obj_t *button = lv_event_get_current_target(event);
    intptr_t code = button ? (intptr_t)lv_obj_get_user_data(button) : -1;
    uint32_t next;

    if (!sport || sport->destroying || !sport->visible ||
        sport->transitioning || sport->celebration_visible ||
        sport->stats_view != SPORT_STATS_GOAL_EDITOR ||
        code < 0 || code > 11) {
        return;
    }

    if (code == 10) {
        sport->goal_input = 0;
        sport->goal_input_fresh = false;
    } else if (code == 11) {
        sport->goal_input /= 10U;
        sport->goal_input_fresh = false;
    } else {
        if (sport->goal_input_fresh || sport->goal_input == 0U) {
            next = (uint32_t)code;
        } else {
            next = sport->goal_input * 10U + (uint32_t)code;
        }
        if (next <= 999999U) {
            sport->goal_input = next;
        }
        sport->goal_input_fresh = false;
    }
    sport_update_goal_editor(sport);
}

static void sport_goal_confirm_clicked(lv_event_t *event)
{
    openvela_ui_sport_t *sport = lv_event_get_user_data(event);
    int metric;
    char hint[72];

    if (!sport || sport->destroying || !sport->visible ||
        sport->transitioning || sport->celebration_visible ||
        sport->stats_view != SPORT_STATS_GOAL_EDITOR) {
        return;
    }
    metric = sport_metric_index(sport->page);
    if (sport->goal_input < sport_goal_minimum(metric) ||
        sport->goal_input > sport_goal_maximum(metric)) {
        snprintf(hint, sizeof(hint),
                 sport_has_chinese(sport) ? "目标需在 %lu～%lu 之间" :
                                            "GOAL MUST BE %lu - %lu",
                 (unsigned long)sport_goal_minimum(metric),
                 (unsigned long)sport_goal_maximum(metric));
        lv_label_set_text(sport->goal_input_hint, hint);
        lv_obj_set_style_text_color(sport->goal_input_hint,
                                    lv_color_hex(0xd13c55), 0);
        return;
    }

    sport->goals[metric] = sport->goal_input;
    sport->persist_dirty = true;
    sport_save_state(sport);
    sport_queue_stats_view(sport, SPORT_STATS_GOAL, -1);
    sport_check_celebration(sport);
}

static void sport_celebration_clicked(lv_event_t *event)
{
    openvela_ui_sport_t *sport = lv_event_get_user_data(event);

    if (!sport || sport->destroying || !sport->celebration_visible) {
        return;
    }
    sport->celebration_visible = false;
    sport_set_hidden(sport->celebration_layer, true);
    if (sport->visible) {
        sport_start_page_animation(sport);
    }
}

static void sport_begin_special(openvela_ui_sport_t *sport)
{
    sport->special_active = true;
    sport->heart_rate = 112;
    sport->last_heart_tick = lv_tick_get();
    lv_label_set_text(sport->enter_button_label,
                      sport_has_chinese(sport) ? "进入运动" : "START");
    lv_obj_add_flag(sport->enter_button, LV_OBJ_FLAG_CLICKABLE);
    if (!sport_queue_page(sport, OPENVELA_UI_SPORT_PAGE_STEPS, 1,
                          true,
                          OPENVELA_UI_SPORT_EVENT_SPECIAL_STARTED)) {
        sport_queue_event(sport,
                          OPENVELA_UI_SPORT_EVENT_SPECIAL_STARTED);
    }
}

static void sport_enter_timer_cb(lv_timer_t *timer)
{
    openvela_ui_sport_t *sport = lv_timer_get_user_data(timer);

    if (!sport || sport->destroying) {
        return;
    }
    sport->enter_timer = NULL;
    sport_begin_special(sport);
}

static void sport_cancel_enter_feedback(openvela_ui_sport_t *sport)
{
    if (sport->enter_timer) {
        lv_timer_delete(sport->enter_timer);
        sport->enter_timer = NULL;
    }
    if (sport->enter_button_label &&
        lv_obj_is_valid(sport->enter_button_label)) {
        lv_label_set_text(sport->enter_button_label,
                          sport_has_chinese(sport) ?
                              "进入运动" : "START");
    }
    if (sport->enter_button && lv_obj_is_valid(sport->enter_button)) {
        lv_obj_add_flag(sport->enter_button, LV_OBJ_FLAG_CLICKABLE);
    }
}

static void sport_enter_clicked(lv_event_t *event)
{
    openvela_ui_sport_t *sport = lv_event_get_user_data(event);

    if (!sport || sport->destroying || !sport->visible ||
        sport->transitioning || sport->confirm_visible ||
        sport->page != OPENVELA_UI_SPORT_PAGE_ENTRY || sport->enter_timer) {
        return;
    }

    lv_label_set_text(sport->enter_button_label,
                      sport_has_chinese(sport) ? "正在进入" : "STARTING");
    lv_obj_clear_flag(sport->enter_button, LV_OBJ_FLAG_CLICKABLE);
    sport_stop_page_animations(sport);
    sport->enter_timer = lv_timer_create(sport_enter_timer_cb,
                                         SPORT_ENTER_FEEDBACK_MS, sport);
    if (sport->enter_timer) {
        lv_timer_set_repeat_count(sport->enter_timer, 1);
    } else {
        sport_begin_special(sport);
    }
}

static void sport_exit_clicked(lv_event_t *event)
{
    openvela_ui_sport_t *sport = lv_event_get_user_data(event);

    if (!sport || sport->destroying || !sport->visible ||
        sport->transitioning || sport->confirm_visible ||
        sport->page != OPENVELA_UI_SPORT_PAGE_EXIT) {
        return;
    }
    sport->confirm_visible = true;
    sport_stop_page_animations(sport);
    sport_set_hidden(sport->confirm_layer, false);
    lv_obj_move_foreground(sport->confirm_layer);
}

static void sport_exit_cancel_clicked(lv_event_t *event)
{
    openvela_ui_sport_t *sport = lv_event_get_user_data(event);

    if (!sport || sport->destroying || !sport->confirm_visible) {
        return;
    }
    sport->confirm_visible = false;
    sport_set_hidden(sport->confirm_layer, true);
    sport_start_page_animation(sport);
}

static void sport_exit_confirm_clicked(lv_event_t *event)
{
    openvela_ui_sport_t *sport = lv_event_get_user_data(event);

    if (!sport || sport->destroying || !sport->confirm_visible ||
        sport->transitioning) {
        return;
    }
    sport->confirm_visible = false;
    sport_set_hidden(sport->confirm_layer, true);
    sport->special_active = false;
    sport->external_heart = false;
    if (!sport_queue_page(sport, OPENVELA_UI_SPORT_PAGE_ENTRY, 1,
                          true,
                          OPENVELA_UI_SPORT_EVENT_SPECIAL_ENDED)) {
        sport_queue_event(sport,
                          OPENVELA_UI_SPORT_EVENT_SPECIAL_ENDED);
    }
}

openvela_ui_sport_t *openvela_ui_sport_create(
    lv_obj_t *parent,
    int32_t width,
    int32_t height,
    int32_t scale_1000,
    const openvela_ui_sport_fonts_t *fonts,
    const char *action_preview_path)
{
    openvela_ui_sport_t *sport;

    if (!parent || !lv_obj_is_valid(parent) || width <= 0 || height <= 0) {
        return NULL;
    }

    sport = calloc(1, sizeof(*sport));
    if (!sport) {
        return NULL;
    }

    sport->width = width;
    sport->height = height;
    sport->scale_1000 = scale_1000 > 0 ? scale_1000 :
        (width * 1000) / SPORT_DESIGN_WIDTH;
    if (fonts) {
        sport->fonts = *fonts;
    }
    if (action_preview_path) {
        snprintf(sport->action_preview_path,
                 sizeof(sport->action_preview_path), "%s",
                 action_preview_path);
    }

    sport->page = OPENVELA_UI_SPORT_PAGE_ENTRY;
    sport->pending_page = sport->page;
    sport->stats_view = SPORT_STATS_TODAY;
    sport->pending_stats_view = SPORT_STATS_TODAY;
    sport->chart_bubble_hour = -1;
    sport_load_state(sport);
    sport->heart_rate = 112U;
    sport->random_state = (uint32_t)(lv_tick_get() ^
                           (uintptr_t)sport ^ (uintptr_t)parent);
    sport->last_step_tick = lv_tick_get();
    sport->last_heart_tick = sport->last_step_tick;
    sport_seed_hours(sport);

    sport->root = lv_obj_create(parent);
    lv_obj_remove_style_all(sport->root);
    lv_obj_set_pos(sport->root, 0, 0);
    lv_obj_set_size(sport->root, width, height);
    lv_obj_set_style_bg_color(sport->root, lv_color_hex(0x07122f), 0);
    lv_obj_set_style_bg_opa(sport->root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(sport->root,
                      LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    /* Keep the ancestor hidden while all pages and images are constructed.
     * The caller reveals the complete tree once on a later LVGL turn. */
    lv_obj_add_flag(sport->root, LV_OBJ_FLAG_HIDDEN);

    sport_create_background(sport);
    sport_create_entry_page(sport);
    sport_create_stats_page(sport);
    sport_create_history_page(sport);
    sport_create_goal_page(sport);
    sport_create_goal_editor_page(sport);
    sport_create_heart_page(sport);
    sport_create_exit_page(sport);
    sport_create_confirm_layer(sport);
    sport_create_celebration_layer(sport);

    sport_update_stats(sport);
    sport_update_history(sport);
    sport->selected_history = 1;
    sport_update_goal(sport);
    sport->goal_input = sport->goals[0];
    sport->goal_input_fresh = true;
    sport_update_goal_editor(sport);
    sport_update_heart(sport);
    sport->data_timer = lv_timer_create(sport_data_tick, 250, sport);
    return sport;
}

void openvela_ui_sport_destroy(openvela_ui_sport_t *sport)
{
    if (!sport || sport->destroying) {
        return;
    }

    sport->destroying = true;
    lv_async_call_cancel(sport_transition_begin_async, sport);
    lv_async_call_cancel(sport_event_async, sport);
    sport_stop_page_animations(sport);
    sport_cancel_enter_feedback(sport);
    sport_hide_chart_bubble(sport);
    sport_save_state(sport);
    if (sport->data_timer) {
        lv_timer_delete(sport->data_timer);
        sport->data_timer = NULL;
    }
    if (sport->chart_bubble_timer) {
        lv_timer_delete(sport->chart_bubble_timer);
        sport->chart_bubble_timer = NULL;
    }
    if (sport->celebration_layer &&
        lv_obj_is_valid(sport->celebration_layer)) {
        lv_obj_delete(sport->celebration_layer);
        sport->celebration_layer = NULL;
    }
    if (sport->root && lv_obj_is_valid(sport->root)) {
        lv_obj_delete(sport->root);
    }
    free(sport);
}

void openvela_ui_sport_shown(openvela_ui_sport_t *sport)
{
    if (!sport || sport->destroying) {
        return;
    }
    sport->visible = true;
    sport_set_hidden(sport->root, false);
    sport_start_page_animation(sport);
    sport_check_celebration(sport);
}

void openvela_ui_sport_hidden(openvela_ui_sport_t *sport)
{
    if (!sport || sport->destroying) {
        return;
    }
    sport->visible = false;
    sport_cancel_enter_feedback(sport);
    sport_hide_chart_bubble(sport);
    sport_save_state(sport);
    sport->confirm_visible = false;
    sport_set_hidden(sport->confirm_layer, true);
    if (!sport->special_active && sport->celebration_visible) {
        sport->celebration_visible = false;
        sport_set_hidden(sport->celebration_layer, true);
    }
    sport_stop_page_animations(sport);
    sport_set_hidden(sport->root, true);
}

void openvela_ui_sport_set_low_power(openvela_ui_sport_t *sport,
                                     bool low_power)
{
    if (!sport || sport->destroying) {
        return;
    }

    sport->low_power = low_power;
    if (sport->data_timer) {
        /* Keep accounting/heart simulation cadence unchanged.  Low power
         * suppresses rendering below instead of slowing business time. */
        lv_timer_set_period(sport->data_timer, 250U);
        lv_timer_reset(sport->data_timer);
    }
    if (low_power) {
        sport_hide_chart_bubble(sport);
        sport_stop_page_animations(sport);
        return;
    }

    if (sport->stats_view == SPORT_STATS_TODAY &&
        sport->page >= OPENVELA_UI_SPORT_PAGE_STEPS &&
        sport->page <= OPENVELA_UI_SPORT_PAGE_DURATION) {
        sport_update_stats(sport);
    } else if (sport->stats_view == SPORT_STATS_HISTORY) {
        sport_update_history(sport);
    } else if (sport->stats_view == SPORT_STATS_HISTORY_DETAIL) {
        sport_update_history_detail(sport);
    } else if (sport->stats_view == SPORT_STATS_GOAL) {
        sport_update_goal(sport);
    } else if (sport->stats_view == SPORT_STATS_GOAL_EDITOR) {
        sport_update_goal_editor(sport);
    } else if (sport->page == OPENVELA_UI_SPORT_PAGE_HEART_RATE) {
        sport_update_heart(sport);
    }
    sport_start_page_animation(sport);
    sport_check_celebration(sport);
}

void openvela_ui_sport_set_event_cb(
    openvela_ui_sport_t *sport,
    openvela_ui_sport_event_cb_t callback,
    void *user_data)
{
    if (!sport || sport->destroying) {
        return;
    }
    sport->event_cb = callback;
    sport->event_user_data = user_data;
}

static bool sport_request_home(openvela_ui_sport_t *sport,
                               openvela_ui_sport_page_t fallback,
                               int direction)
{
    if (sport->event_cb) {
        return sport_queue_event(sport,
                                 OPENVELA_UI_SPORT_EVENT_HOME_REQUESTED);
    }
    return sport_queue_page(sport, fallback, direction, false,
                            OPENVELA_UI_SPORT_EVENT_HOME_REQUESTED);
}

bool openvela_ui_sport_gesture(openvela_ui_sport_t *sport,
                               int32_t delta_x, int32_t delta_y)
{
    int32_t threshold;
    bool horizontal;
    bool vertical;
    bool chart_view;
    bool recent_chart_touch;

    if (!sport || sport->destroying || !sport->visible) {
        return false;
    }
    if (sport->transitioning || sport->confirm_visible ||
        sport->celebration_visible || sport->enter_timer) {
        return true;
    }
    chart_view = sport->stats_view == SPORT_STATS_TODAY ||
                 sport->stats_view == SPORT_STATS_HISTORY_DETAIL;
    recent_chart_touch = sport->chart_touch_seen &&
                         lv_tick_elaps(sport->last_chart_touch_tick) <
                             SPORT_CHART_GESTURE_GUARD_MS;
    if (chart_view && (sport->chart_touching || recent_chart_touch)) {
        return true;
    }

    threshold = sport_sx(sport, SPORT_GESTURE_THRESHOLD);
    horizontal = LV_ABS(delta_x) > threshold &&
                 LV_ABS(delta_x) * 100 > LV_ABS(delta_y) * 108;
    vertical = LV_ABS(delta_y) > threshold &&
               LV_ABS(delta_y) * 100 > LV_ABS(delta_x) * 108;
    if (!horizontal && !vertical) {
        return false;
    }

    if (!sport->special_active) {
        if (sport->page == OPENVELA_UI_SPORT_PAGE_ENTRY) {
            if (vertical) {
                return sport_queue_event(
                    sport, OPENVELA_UI_SPORT_EVENT_CLOSE_REQUESTED);
            }
            if (horizontal && delta_x < 0) {
                return sport_queue_page(
                    sport, OPENVELA_UI_SPORT_PAGE_STEPS, 1, false,
                    OPENVELA_UI_SPORT_EVENT_CLOSE_REQUESTED);
            }
            return true;
        }

        if (sport->page >= OPENVELA_UI_SPORT_PAGE_STEPS &&
            sport->page <= OPENVELA_UI_SPORT_PAGE_DURATION) {
            if (sport->stats_view != SPORT_STATS_TODAY) {
                if (!horizontal) {
                    return true;
                }
                if (delta_x < 0 &&
                    sport->stats_view == SPORT_STATS_HISTORY) {
                    return sport_queue_stats_view(
                        sport, SPORT_STATS_GOAL, 1);
                }
                if (delta_x > 0) {
                    if (sport->stats_view == SPORT_STATS_HISTORY) {
                        return sport_queue_stats_view(
                            sport, SPORT_STATS_TODAY, -1);
                    }
                    if (sport->stats_view == SPORT_STATS_HISTORY_DETAIL) {
                        return sport_queue_stats_view(
                            sport, SPORT_STATS_HISTORY, -1);
                    }
                    if (sport->stats_view == SPORT_STATS_GOAL) {
                        return sport_queue_stats_view(
                            sport, SPORT_STATS_HISTORY, -1);
                    }
                    if (sport->stats_view == SPORT_STATS_GOAL_EDITOR) {
                        return sport_queue_stats_view(
                            sport, SPORT_STATS_GOAL, -1);
                    }
                }
                return true;
            }
            if (horizontal && delta_x > 0) {
                return sport_queue_page(
                    sport, OPENVELA_UI_SPORT_PAGE_ENTRY, -1, false,
                    OPENVELA_UI_SPORT_EVENT_CLOSE_REQUESTED);
            }
            if (horizontal && delta_x < 0) {
                return sport_queue_stats_view(
                    sport, SPORT_STATS_HISTORY, 1);
            }
            if (vertical && delta_y < 0) {
                if (sport->page < OPENVELA_UI_SPORT_PAGE_DURATION) {
                    return sport_queue_page(
                        sport, (openvela_ui_sport_page_t)(sport->page + 1),
                        1, false,
                        OPENVELA_UI_SPORT_EVENT_CLOSE_REQUESTED);
                }
                return true;
            }
            if (vertical && delta_y > 0) {
                if (sport->page == OPENVELA_UI_SPORT_PAGE_STEPS) {
                    return sport_queue_event(
                        sport, OPENVELA_UI_SPORT_EVENT_CLOSE_REQUESTED);
                }
                return sport_queue_page(
                    sport, (openvela_ui_sport_page_t)(sport->page - 1),
                    -1, false,
                    OPENVELA_UI_SPORT_EVENT_CLOSE_REQUESTED);
            }
            return true;
        }
        return true;
    }

    if (!horizontal) {
        return true;
    }
    if (delta_x < 0) {
        if (sport->page == OPENVELA_UI_SPORT_PAGE_STEPS) {
            return sport_queue_page(sport,
                OPENVELA_UI_SPORT_PAGE_CALORIES, 1, false,
                OPENVELA_UI_SPORT_EVENT_HOME_REQUESTED);
        }
        if (sport->page == OPENVELA_UI_SPORT_PAGE_CALORIES) {
            return sport_queue_page(sport,
                OPENVELA_UI_SPORT_PAGE_DURATION, 1, false,
                OPENVELA_UI_SPORT_EVENT_HOME_REQUESTED);
        }
        if (sport->page == OPENVELA_UI_SPORT_PAGE_DURATION) {
            return sport_queue_page(sport,
                OPENVELA_UI_SPORT_PAGE_HEART_RATE, 1, false,
                OPENVELA_UI_SPORT_EVENT_HOME_REQUESTED);
        }
        if (sport->page == OPENVELA_UI_SPORT_PAGE_HEART_RATE) {
            return sport_queue_page(sport,
                OPENVELA_UI_SPORT_PAGE_EXIT, 1, false,
                OPENVELA_UI_SPORT_EVENT_HOME_REQUESTED);
        }
        if (sport->page == OPENVELA_UI_SPORT_PAGE_EXIT) {
            return sport_request_home(sport,
                OPENVELA_UI_SPORT_PAGE_STEPS, 1);
        }
        return sport_queue_page(sport,
            OPENVELA_UI_SPORT_PAGE_STEPS, 1, false,
            OPENVELA_UI_SPORT_EVENT_HOME_REQUESTED);
    }

    if (sport->page == OPENVELA_UI_SPORT_PAGE_EXIT) {
        return sport_queue_page(sport,
            OPENVELA_UI_SPORT_PAGE_HEART_RATE, -1, false,
            OPENVELA_UI_SPORT_EVENT_HOME_REQUESTED);
    }
    if (sport->page == OPENVELA_UI_SPORT_PAGE_HEART_RATE) {
        return sport_queue_page(sport,
            OPENVELA_UI_SPORT_PAGE_DURATION, -1, false,
            OPENVELA_UI_SPORT_EVENT_HOME_REQUESTED);
    }
    if (sport->page == OPENVELA_UI_SPORT_PAGE_DURATION) {
        return sport_queue_page(sport,
            OPENVELA_UI_SPORT_PAGE_CALORIES, -1, false,
            OPENVELA_UI_SPORT_EVENT_HOME_REQUESTED);
    }
    if (sport->page == OPENVELA_UI_SPORT_PAGE_CALORIES) {
        return sport_queue_page(sport,
            OPENVELA_UI_SPORT_PAGE_STEPS, -1, false,
            OPENVELA_UI_SPORT_EVENT_HOME_REQUESTED);
    }
    if (sport->page == OPENVELA_UI_SPORT_PAGE_STEPS) {
        return sport_request_home(sport,
            OPENVELA_UI_SPORT_PAGE_EXIT, -1);
    }
    return true;
}

bool openvela_ui_sport_show_page(openvela_ui_sport_t *sport,
                                 openvela_ui_sport_page_t page)
{
    int direction;

    if (!sport || sport->destroying || !sport_page_valid(page)) {
        return false;
    }
    if (page == sport->page) {
        if (sport->stats_view == SPORT_STATS_TODAY) {
            return true;
        }
        return sport_queue_page(sport, page, 1, false,
                                OPENVELA_UI_SPORT_EVENT_HOME_REQUESTED);
    }
    direction = page > sport->page ? 1 : -1;
    return sport_queue_page(sport, page, direction, false,
                            OPENVELA_UI_SPORT_EVENT_HOME_REQUESTED);
}

void openvela_ui_sport_set_data(openvela_ui_sport_t *sport,
                                uint32_t steps, uint16_t heart_rate)
{
    if (!sport || sport->destroying) {
        return;
    }
    if (steps > 0U) {
        sport->steps = steps > SPORT_MAX_STEPS ?
            SPORT_MAX_STEPS : steps;
        sport->external_steps = true;
        sport->persist_dirty = true;
        sport_update_hour_total(sport);
        if (sport->stats_view == SPORT_STATS_TODAY &&
            sport->page >= OPENVELA_UI_SPORT_PAGE_STEPS &&
            sport->page <= OPENVELA_UI_SPORT_PAGE_DURATION) {
            sport_update_stats(sport);
        } else if (sport->stats_view == SPORT_STATS_HISTORY) {
            sport_update_history(sport);
        } else if (sport->stats_view == SPORT_STATS_GOAL) {
            sport_update_goal(sport);
        }
        sport_check_celebration(sport);
    }
    if (heart_rate >= 30U && heart_rate <= 220U) {
        sport->heart_rate = heart_rate;
        sport->external_heart = true;
        sport_update_heart(sport);
    }
}

void openvela_ui_sport_set_action_preview(
    openvela_ui_sport_t *sport,
    const char *action_preview_path)
{
    if (!sport || sport->destroying || !action_preview_path ||
        !action_preview_path[0] || access(action_preview_path, R_OK) != 0) {
        return;
    }

    snprintf(sport->action_preview_path,
             sizeof(sport->action_preview_path), "%s",
             action_preview_path);
    if (sport->entry_cat_image &&
        lv_obj_is_valid(sport->entry_cat_image)) {
        lv_image_set_src(sport->entry_cat_image,
                         sport->action_preview_path);
    }
    if (sport->exit_cat_image && lv_obj_is_valid(sport->exit_cat_image)) {
        lv_image_set_src(sport->exit_cat_image,
                         sport->action_preview_path);
    }
}

void openvela_ui_sport_set_special_active(openvela_ui_sport_t *sport,
                                          bool active)
{
    if (!sport || sport->destroying) {
        return;
    }
    sport->special_active = active;
    if (!active) {
        sport_cancel_enter_feedback(sport);
        sport->confirm_visible = false;
        sport_set_hidden(sport->confirm_layer, true);
    } else {
        sport->last_heart_tick = lv_tick_get();
    }
}

bool openvela_ui_sport_is_special_active(
    const openvela_ui_sport_t *sport)
{
    return sport && !sport->destroying && sport->special_active;
}

bool openvela_ui_sport_is_transitioning(
    const openvela_ui_sport_t *sport)
{
    return sport && !sport->destroying && sport->transitioning;
}

openvela_ui_sport_page_t openvela_ui_sport_current_page(
    const openvela_ui_sport_t *sport)
{
    return sport && !sport->destroying ?
        sport->page : OPENVELA_UI_SPORT_PAGE_ENTRY;
}

lv_obj_t *openvela_ui_sport_root(openvela_ui_sport_t *sport)
{
    return sport && !sport->destroying ? sport->root : NULL;
}
