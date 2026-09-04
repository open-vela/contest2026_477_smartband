#include <nuttx/config.h>

#include <errno.h>
#include <lvgl/lvgl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "openvela_ui_agent_bridge.h"

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
#include "core/message_bus.h"
#include "core/message_bus_tap.h"
#endif

#define MOONCAT_AGENT_CHANNEL "mooncat"
#define MOONCAT_NOTIFICATION_TIMEOUT_MS 8000

static pthread_mutex_t g_bridge_lock = PTHREAD_MUTEX_INITIALIZER;
static char *g_pending_message;
static lv_obj_t *g_notification_card;
static lv_timer_t *g_hide_timer;

static void mooncat_hide_notification(lv_timer_t *timer)
{
    if (g_notification_card && lv_obj_is_valid(g_notification_card)) {
        lv_obj_delete(g_notification_card);
    }

    g_notification_card = NULL;
    g_hide_timer = NULL;
    lv_timer_delete(timer);
}

static void mooncat_render_notification(const char *message)
{
    lv_obj_t *body;
    lv_obj_t *card;
    lv_obj_t *title;

    if (!message || !message[0] || !lv_screen_active()) {
        return;
    }

    if (g_hide_timer) {
        lv_timer_delete(g_hide_timer);
        g_hide_timer = NULL;
    }

    if (g_notification_card && lv_obj_is_valid(g_notification_card)) {
        lv_obj_delete(g_notification_card);
    }

    card = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 300, 92);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x182235), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x67E8F9), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    title = lv_label_create(card);
    lv_obj_remove_style_all(title);
    lv_label_set_text(title, "MOONCAT ACTIVE COACH");
    lv_obj_set_width(title, 276);
    lv_obj_set_style_text_color(title, lv_color_hex(0x67E8F9), 0);
    lv_obj_set_style_text_font(title, LV_FONT_DEFAULT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    body = lv_label_create(card);
    lv_obj_remove_style_all(body);
    lv_label_set_text(body, message);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, 276);
    lv_obj_set_style_text_color(body, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(body, LV_FONT_DEFAULT, 0);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, 24);

    lv_obj_move_foreground(card);
    g_notification_card = card;
    g_hide_timer = lv_timer_create(mooncat_hide_notification,
                                   MOONCAT_NOTIFICATION_TIMEOUT_MS, NULL);
}

static void mooncat_show_pending_async(void *unused)
{
    char *message;

    (void)unused;
    pthread_mutex_lock(&g_bridge_lock);
    message = g_pending_message;
    g_pending_message = NULL;
    pthread_mutex_unlock(&g_bridge_lock);

    if (message) {
        mooncat_render_notification(message);
        free(message);
    }
}

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
/* Runs in the agent outbound-dispatch task.  It only copies and schedules. */
static void mooncat_agent_tap(const agent_msg_t *message, void *cookie)
{
    char *copy;

    (void)cookie;
    if (!message || !message->content || !message->content[0]) {
        return;
    }

    copy = strdup(message->content);
    if (!copy) {
        syslog(LOG_ERR, "[mooncat_bridge] notification copy failed\n");
        return;
    }

    pthread_mutex_lock(&g_bridge_lock);
    free(g_pending_message);
    g_pending_message = copy;
    pthread_mutex_unlock(&g_bridge_lock);

    /* LVGL object access happens only in mooncat_show_pending_async(). */
    if (lv_async_call(mooncat_show_pending_async, NULL) != LV_RESULT_OK) {
        pthread_mutex_lock(&g_bridge_lock);
        if (g_pending_message == copy) {
            g_pending_message = NULL;
            free(copy);
        }
        pthread_mutex_unlock(&g_bridge_lock);
        syslog(LOG_ERR, "[mooncat_bridge] LVGL async queue rejected message\n");
    }
}
#endif

int openvela_ui_agent_bridge_start(void)
{
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
    int result = mbus_tap_register(MOONCAT_AGENT_CHANNEL,
                                   mooncat_agent_tap, NULL);

    if (result != OK) {
        syslog(LOG_ERR,
               "[mooncat_bridge] channel tap registration failed\n");
        return -EIO;
    }

    syslog(LOG_INFO,
           "[mooncat_bridge] listening on outbound channel '%s'\n",
           MOONCAT_AGENT_CHANNEL);
    return 0;
#else
    syslog(LOG_WARNING,
           "[mooncat_bridge] ai_agent disabled; proactive UI unavailable\n");
    return -ENOSYS;
#endif
}

void openvela_ui_agent_bridge_stop(void)
{
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
    mbus_tap_unregister(MOONCAT_AGENT_CHANNEL);
#endif

    lv_async_call_cancel(mooncat_show_pending_async, NULL);
    pthread_mutex_lock(&g_bridge_lock);
    free(g_pending_message);
    g_pending_message = NULL;
    pthread_mutex_unlock(&g_bridge_lock);

    if (g_hide_timer) {
        lv_timer_delete(g_hide_timer);
        g_hide_timer = NULL;
    }
    if (g_notification_card && lv_obj_is_valid(g_notification_card)) {
        lv_obj_delete(g_notification_card);
    }
    g_notification_card = NULL;
}
