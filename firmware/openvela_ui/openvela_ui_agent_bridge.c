#include <nuttx/config.h>

#include <errno.h>
#include <lvgl/lvgl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#include "openvela_ui_agent_bridge.h"
#include "openvela_ui_style.h"

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
#include "core/message_bus.h"
#include "core/message_bus_tap.h"
#endif

#define MOONCAT_AGENT_CHANNEL "mooncat"
#define MOONCAT_NOTIFICATION_TIMEOUT_MS 8000
#define MOONCAT_BRIDGE_POLL_MS 100

static pthread_mutex_t g_bridge_lock = PTHREAD_MUTEX_INITIALIZER;
#define MOONCAT_NOTIFICATION_CAPACITY 8
#define MOONCAT_NOTIFICATION_BYTES 1024
static char *g_messages[MOONCAT_NOTIFICATION_CAPACITY];
static unsigned g_head,g_count;
static int g_accepting;
/* Debugger-visible aggregate; rejected messages never evict accepted ones. */
static unsigned g_rejected_notifications;
static lv_obj_t *g_notification_card;
static lv_timer_t *g_hide_timer;
static lv_timer_t *g_pending_timer;
static uint32_t g_displayed_at;

static void mooncat_hide_notification(lv_timer_t *timer)
{
    if (g_notification_card && lv_obj_is_valid(g_notification_card)) {
        lv_obj_delete(g_notification_card);
    }

    g_notification_card = NULL;
    g_hide_timer = NULL;
    if(timer) lv_timer_delete(timer);
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

    card = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 270, 96);
    lv_obj_align(card, LV_ALIGN_TOP_MID, 0, 8);
    mooncat_surface(card);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    title = lv_label_create(card);
    lv_obj_remove_style_all(title);
    lv_label_set_text(title, "A note from MoonCat");
    lv_obj_set_width(title, 248);
    lv_obj_set_style_text_color(title, lv_color_hex(MOONCAT_MUTED), 0);
    lv_obj_set_style_text_font(title, LV_FONT_DEFAULT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    body = lv_label_create(card);
    lv_obj_remove_style_all(body);
    lv_label_set_text(body, message);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, 248);
    lv_obj_set_height(body, 48);
    lv_obj_set_style_text_color(body, lv_color_hex(MOONCAT_INK), 0);
    lv_obj_set_style_text_font(body, LV_FONT_DEFAULT, 0);
    lv_obj_align(body, LV_ALIGN_TOP_LEFT, 0, 24);

    lv_obj_move_foreground(card);
    g_notification_card = card;
    g_displayed_at=lv_tick_get();
    g_hide_timer = lv_timer_create(mooncat_hide_notification,
                                   MOONCAT_NOTIFICATION_TIMEOUT_MS, NULL);
}

static void mooncat_show_pending(lv_timer_t *timer)
{
    char *message;

    (void)timer;
    if(g_notification_card) {
        if(g_hide_timer || lv_tick_elaps(g_displayed_at)<MOONCAT_NOTIFICATION_TIMEOUT_MS) return;
        mooncat_hide_notification(NULL);
    }
    pthread_mutex_lock(&g_bridge_lock);
    message = NULL;
    if(g_count) {
        message=g_messages[g_head];g_messages[g_head]=NULL;
        g_head=(g_head+1)%MOONCAT_NOTIFICATION_CAPACITY;g_count--;
    }
    pthread_mutex_unlock(&g_bridge_lock);

    if (message) {
        mooncat_render_notification(message);
        free(message);
    }
}

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
/* Runs in the agent task: only publish data, never call LVGL here.
 * lv_async_call() also allocates an LVGL timer and touches LVGL state. */
static void mooncat_agent_tap(const agent_msg_t *message, void *cookie)
{
    char *copy;

    (void)cookie;
    if (!message || !message->content || !message->content[0]) {
        return;
    }

    if(strnlen(message->content,MOONCAT_NOTIFICATION_BYTES+1)>MOONCAT_NOTIFICATION_BYTES) {
        pthread_mutex_lock(&g_bridge_lock);g_rejected_notifications++;pthread_mutex_unlock(&g_bridge_lock);
        syslog(LOG_WARNING,"[mooncat_bridge] notification too long; rejected\n");return;
    }
    copy = strdup(message->content);
    if (!copy) {
        syslog(LOG_ERR, "[mooncat_bridge] notification copy failed\n");
        return;
    }

    pthread_mutex_lock(&g_bridge_lock);
    if(!g_accepting || g_count==MOONCAT_NOTIFICATION_CAPACITY) {
        g_rejected_notifications++;
        pthread_mutex_unlock(&g_bridge_lock);free(copy);
        syslog(LOG_WARNING,"[mooncat_bridge] notification queue unavailable; rejected newest\n");return;
    }
    g_messages[(g_head+g_count)%MOONCAT_NOTIFICATION_CAPACITY]=copy;g_count++;
    pthread_mutex_unlock(&g_bridge_lock);

}
#endif

int openvela_ui_agent_bridge_start(void)
{
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
    /* Start/stop are called by the Native UI task after LVGL initialization. */
    if(g_pending_timer) return 0;
    g_pending_timer = lv_timer_create(mooncat_show_pending,
                                      MOONCAT_BRIDGE_POLL_MS, NULL);
    if (!g_pending_timer) {
        return -ENOMEM;
    }

    pthread_mutex_lock(&g_bridge_lock);g_accepting=1;pthread_mutex_unlock(&g_bridge_lock);
    int result = mbus_tap_register(MOONCAT_AGENT_CHANNEL,
                                   mooncat_agent_tap, NULL);

    if (result != OK) {
        pthread_mutex_lock(&g_bridge_lock);g_accepting=0;pthread_mutex_unlock(&g_bridge_lock);
        lv_timer_delete(g_pending_timer);
        g_pending_timer = NULL;
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
    pthread_mutex_lock(&g_bridge_lock);g_accepting=0;pthread_mutex_unlock(&g_bridge_lock);
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
    mbus_tap_unregister(MOONCAT_AGENT_CHANNEL);
#endif

    if (g_pending_timer) {
        lv_timer_delete(g_pending_timer);
        g_pending_timer = NULL;
    }
    pthread_mutex_lock(&g_bridge_lock);
    for(unsigned i=0;i<MOONCAT_NOTIFICATION_CAPACITY;i++) {free(g_messages[i]);g_messages[i]=NULL;}
    g_head=g_count=0;
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
