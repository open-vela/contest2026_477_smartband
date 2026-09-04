#include "mooncat_local_router.h"

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent_compat.h"

#define MAX_CALLS 8
#define CALL_INPUT_SIZE 1200

struct captured_call_s {
    char name[48];
    char input[CALL_INPUT_SIZE];
};

static struct captured_call_s g_calls[MAX_CALLS];
static int g_call_count;
static int g_coach_status;
static int g_time_status;
static int g_list_status;
static int g_add_status;
static const char *g_coach_output;
static const char *g_time_output;
static const char *g_list_output;
static const char *g_add_output;

static const char *const COACH_EXECUTE_JSON =
    "{\"mode\":\"execute\",\"source\":\"simulated\",\"valid\":true,"
    "\"workout_active\":false,\"do_not_disturb\":false,"
    "\"inactivity_minutes\":75,\"sleep_debt_minutes\":40,"
    "\"stress_score\":72,\"battery_percent\":68}";

static const char *const COACH_PREVIEW_JSON =
    "{\"mode\":\"preview\",\"source\":\"simulated\",\"valid\":true,"
    "\"workout_active\":false,\"do_not_disturb\":false,"
    "\"inactivity_minutes\":75,\"sleep_debt_minutes\":40,"
    "\"stress_score\":72,\"battery_percent\":68}";

static const char *const CRON_ADD_JSON =
    "{\"name\":\"mooncat-demo-once\",\"schedule_type\":\"at\","
    "\"at_epoch\":2000000020,"
    "\"message\":\"[DEMO] MoonCat proactive check completed\","
    "\"channel\":\"system\",\"chat_id\":\"mooncat-coach\","
    "\"action\":\"mooncat_coach_tick\","
    "\"action_args\":\"{\\\"mode\\\":\\\"execute\\\","
    "\\\"source\\\":\\\"simulated\\\",\\\"valid\\\":true,"
    "\\\"workout_active\\\":false,\\\"do_not_disturb\\\":false,"
    "\\\"inactivity_minutes\\\":75,\\\"sleep_debt_minutes\\\":40,"
    "\\\"stress_score\\\":72,\\\"battery_percent\\\":68}\"}";

static void reset_tools(void)
{
    memset(g_calls, 0, sizeof(g_calls));
    g_call_count = 0;
    g_coach_status = OK;
    g_time_status = OK;
    g_list_status = OK;
    g_add_status = OK;
    g_coach_output = "{\"ok\":true,\"status\":\"delivered\"}";
    g_time_output =
        "2033-05-18 11:33:20 CST (UTC+8), UNIX epoch: 2000000000";
    g_list_output = "No cron jobs scheduled.";
    g_add_output = "OK: Added one-shot job 'mooncat-demo-once'.";
}

static void capture_call(const char *name, const char *input)
{
    assert(g_call_count < MAX_CALLS);
    snprintf(g_calls[g_call_count].name, sizeof(g_calls[g_call_count].name),
             "%s", name);
    snprintf(g_calls[g_call_count].input, sizeof(g_calls[g_call_count].input),
             "%s", input);
    g_call_count++;
}

static void write_output(char *output, size_t output_size, const char *text)
{
    if (output && output_size > 0) {
        snprintf(output, output_size, "%s", text ? text : "");
    }
}

int tool_registry_execute(const char *name, const char *input_json,
                          char *output, size_t output_size)
{
    capture_call(name, input_json);
    if (strcmp(name, "mooncat_coach_tick") == 0) {
        write_output(output, output_size, g_coach_output);
        return g_coach_status;
    }
    if (strcmp(name, "get_current_time") == 0) {
        write_output(output, output_size, g_time_output);
        return g_time_status;
    }
    if (strcmp(name, "cron_list") == 0) {
        write_output(output, output_size, g_list_output);
        return g_list_status;
    }
    if (strcmp(name, "cron_add") == 0) {
        write_output(output, output_size, g_add_output);
        return g_add_status;
    }

    write_output(output, output_size, "unexpected tool");
    return ERROR;
}

int mooncat_intent_model_predict(
    const char *text, struct mooncat_router_prediction_s *prediction)
{
    prediction->confidence = 0.996f;
    prediction->margin = 0.95f;

    if (strstr(text, "route:now")) {
        prediction->intent = MOONCAT_INTENT_COACH_NOW;
    } else if (strstr(text, "route:preview")) {
        prediction->intent = MOONCAT_INTENT_COACH_PREVIEW;
    } else if (strstr(text, "route:schedule")) {
        prediction->intent = MOONCAT_INTENT_COACH_SCHEDULE;
    } else if (strstr(text, "route:list")) {
        prediction->intent = MOONCAT_INTENT_COACH_LIST;
    } else {
        prediction->intent = MOONCAT_INTENT_FALLBACK;
    }
    return OK;
}

static void assert_call(int index, const char *name, const char *input)
{
    assert(index < g_call_count);
    assert(strcmp(g_calls[index].name, name) == 0);
    assert(strcmp(g_calls[index].input, input) == 0);
}

static void assert_failed_reply(char *reply, const char *reason)
{
    assert(reply != NULL);
    assert(strstr(reply, reason) != NULL);
    assert(strstr(reply, "成功") == NULL);
    free(reply);
}

static int feature_nonzero(const uint8_t *features, size_t size)
{
    int count = 0;

    for (size_t i = 0; i < size; i++) {
        count += features[i] != 0;
    }
    return count;
}

static int hex_value(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

static int print_feature_indices(const char *hex)
{
    size_t hex_length = strlen(hex);
    size_t text_length;
    char *text;
    uint8_t features[MOONCAT_ROUTER_FEATURE_COUNT];
    bool first = true;

    if (hex_length % 2 != 0) {
        return 2;
    }
    text_length = hex_length / 2;
    text = malloc(text_length + 1);
    if (!text) {
        return 3;
    }

    for (size_t i = 0; i < text_length; i++) {
        int high = hex_value(hex[i * 2]);
        int low = hex_value(hex[i * 2 + 1]);

        if (high < 0 || low < 0) {
            free(text);
            return 2;
        }
        text[i] = (char)((high << 4) | low);
    }
    text[text_length] = '\0';

    mooncat_router_featurize(text, features, sizeof(features));
    free(text);
    for (size_t i = 0; i < sizeof(features); i++) {
        if (features[i]) {
            printf("%s%zu", first ? "" : ",", i);
            first = false;
        }
    }
    putchar('\n');
    return 0;
}

static void test_utf8_boundaries(void)
{
    uint8_t upper[MOONCAT_ROUTER_FEATURE_COUNT];
    uint8_t lower[MOONCAT_ROUTER_FEATURE_COUNT];
    const unsigned char truncated_two[] = {0xc2, '\0'};
    const unsigned char truncated_three[] = {0xe4, 0xb8, '\0'};
    const unsigned char truncated_four[] = {0xf0, 0x9f, 0x90, '\0'};

    mooncat_router_featurize("ABC", upper, sizeof(upper));
    mooncat_router_featurize("abc", lower, sizeof(lower));
    assert(memcmp(upper, lower, sizeof(upper)) == 0);

    mooncat_router_featurize((const char *)truncated_two, upper, sizeof(upper));
    assert(feature_nonzero(upper, sizeof(upper)) > 0);
    mooncat_router_featurize((const char *)truncated_three, upper,
                             sizeof(upper));
    assert(feature_nonzero(upper, sizeof(upper)) > 0);
    mooncat_router_featurize((const char *)truncated_four, upper,
                             sizeof(upper));
    assert(feature_nonzero(upper, sizeof(upper)) > 0);
}

static void test_now_preview_and_list(void)
{
    char *reply;

    reset_tools();
    reply = mooncat_local_router_handle("route:now 现在提醒我活动");
    assert(reply != NULL);
    assert(strstr(reply, "coach_now") != NULL);
    assert_call(0, "mooncat_coach_tick", COACH_EXECUTE_JSON);
    free(reply);

    reset_tools();
    reply = mooncat_local_router_handle("route:preview 只预览恢复建议");
    assert(reply != NULL);
    assert(strstr(reply, "coach_preview") != NULL);
    assert_call(0, "mooncat_coach_tick", COACH_PREVIEW_JSON);
    free(reply);

    reset_tools();
    reply = mooncat_local_router_handle("route:list 列出恢复任务");
    assert(reply != NULL);
    assert_call(0, "cron_list", "{}");
    free(reply);
}

static void test_schedule_exact_json(void)
{
    char *reply;

    reset_tools();
    reply = mooncat_local_router_handle(
        "route:schedule {\"at_epoch\":\"CURRENT_EPOCH_PLUS_20\"}");
    assert(reply != NULL);
    assert(strstr(reply, "coach_schedule") != NULL);
    assert(g_call_count == 3);
    assert_call(0, "get_current_time", "{}");
    assert_call(1, "cron_list", "{}");
    assert_call(2, "cron_add", CRON_ADD_JSON);
    free(reply);
}

static void test_reject_and_fallback_boundaries(void)
{
    char *reply;

    reset_tools();
    reply = mooncat_local_router_handle("route:schedule 稍后安排一次提醒");
    assert(reply != NULL);
    assert(strstr(reply, "缺少 10 秒到 24 小时的延迟，未创建任务") != NULL);
    assert(g_call_count == 0);
    free(reply);

    reset_tools();
    reply = mooncat_local_router_handle(
        "route:schedule CURRENT_EPOCH_PLUS_20oops");
    assert(reply != NULL);
    assert(strstr(reply, "未创建任务") != NULL);
    assert(g_call_count == 0);
    free(reply);

    reset_tools();
    reply = mooncat_local_router_handle("几点了，route:fallback");
    assert(reply == NULL);
    assert(g_call_count == 0);
}

static void test_schedule_failures_and_duplicate(void)
{
    char *reply;
    const char *request = "route:schedule 20秒后提醒我活动";
    char overflow_time[128];

    reset_tools();
    g_time_status = ERROR;
    reply = mooncat_local_router_handle(request);
    assert(g_call_count == 1);
    assert_failed_reply(reply, "获取当前时间失败，未创建任务");

    reset_tools();
    g_time_output = "UNIX epoch: 2000000000garbage";
    reply = mooncat_local_router_handle(request);
    assert(g_call_count == 1);
    assert_failed_reply(reply, "当前时间解析失败，未创建任务");

    reset_tools();
    snprintf(overflow_time, sizeof(overflow_time), "UNIX epoch: %lld",
             LLONG_MAX);
    g_time_output = overflow_time;
    reply = mooncat_local_router_handle(request);
    assert(g_call_count == 1);
    assert_failed_reply(reply, "当前时间解析失败，未创建任务");

    reset_tools();
    g_list_status = ERROR;
    reply = mooncat_local_router_handle(request);
    assert(g_call_count == 2);
    assert_failed_reply(reply, "cron_list 失败，未创建任务");

    reset_tools();
    g_list_output =
        "Scheduled jobs (1):\n  1. [abc] \"mooncat-demo-once\" at 2000000020";
    reply = mooncat_local_router_handle(request);
    assert(reply != NULL);
    assert(g_call_count == 2);
    assert(strstr(reply, "未重复创建") != NULL);
    free(reply);

    reset_tools();
    g_add_status = ERROR;
    reply = mooncat_local_router_handle(request);
    assert(g_call_count == 3);
    assert_call(2, "cron_add", CRON_ADD_JSON);
    assert_failed_reply(reply, "cron_add 失败，未创建任务");
}

static void test_non_schedule_tool_failures(void)
{
    char *reply;

    reset_tools();
    g_coach_status = ERROR;
    reply = mooncat_local_router_handle("route:now");
    assert_failed_reply(reply, "mooncat_coach_tick 失败");

    reset_tools();
    g_list_status = ERROR;
    reply = mooncat_local_router_handle("route:list");
    assert_failed_reply(reply, "cron_list 失败");
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--features-hex") == 0) {
        return print_feature_indices(argv[2]);
    }
    assert(argc == 1);

    test_utf8_boundaries();
    test_now_preview_and_list();
    test_schedule_exact_json();
    test_reject_and_fallback_boundaries();
    test_schedule_failures_and_duplicate();
    test_non_schedule_tool_failures();
    puts("mooncat_local_router: all host behavior cases passed");
    return 0;
}
