#ifndef OPENVELA_UI_SPORT_H
#define OPENVELA_UI_SPORT_H

#include <lvgl/lvgl.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The sport view owns only objects created below `parent`.  It does not
 * register objects in the main UI theme arrays and it never deletes or
 * rebuilds a page while handling an input event.  As with other LVGL APIs,
 * every function in this interface must be called from the LVGL thread.
 */

typedef struct openvela_ui_sport_s openvela_ui_sport_t;

typedef struct {
    const lv_font_t *small;
    const lv_font_t *body;
    const lv_font_t *title;
    const lv_font_t *clock;
} openvela_ui_sport_fonts_t;

typedef enum {
    OPENVELA_UI_SPORT_PAGE_ENTRY = 0,
    OPENVELA_UI_SPORT_PAGE_STEPS,
    OPENVELA_UI_SPORT_PAGE_CALORIES,
    OPENVELA_UI_SPORT_PAGE_DURATION,
    OPENVELA_UI_SPORT_PAGE_HEART_RATE,
    OPENVELA_UI_SPORT_PAGE_EXIT,
} openvela_ui_sport_page_t;

typedef enum {
    /* Normal-mode vertical navigation asks the owner to close the view. */
    OPENVELA_UI_SPORT_EVENT_CLOSE_REQUESTED = 0,
    /* The entry button has enabled the special sport navigation state. */
    OPENVELA_UI_SPORT_EVENT_SPECIAL_STARTED,
    /* A special-mode edge gesture asks the owner to display its home page. */
    OPENVELA_UI_SPORT_EVENT_HOME_REQUESTED,
    /* The user confirmed the red exit action. */
    OPENVELA_UI_SPORT_EVENT_SPECIAL_ENDED,
} openvela_ui_sport_event_t;

typedef void (*openvela_ui_sport_event_cb_t)(
    openvela_ui_sport_t *sport,
    openvela_ui_sport_event_t event,
    void *user_data);

/*
 * `width`, `height`, and `scale_1000` use the same convention as
 * openvela_ui.c (432 x 514 at scale 1000).  `action_preview_path` should be a
 * persistent PNG path while create() runs; the module copies the path.
 */
openvela_ui_sport_t *openvela_ui_sport_create(
    lv_obj_t *parent,
    int32_t width,
    int32_t height,
    int32_t scale_1000,
    const openvela_ui_sport_fonts_t *fonts,
    const char *action_preview_path);

/* Must be called before the owner deletes `parent`. */
void openvela_ui_sport_destroy(openvela_ui_sport_t *sport);

/* Call after the owner's full-screen entry transition has completed. */
void openvela_ui_sport_shown(openvela_ui_sport_t *sport);

/* Stops visual animations but retains the complete persistent object tree. */
void openvela_ui_sport_hidden(openvela_ui_sport_t *sport);

/*
 * Keeps business timers alive while reducing their LVGL wake-up cadence.
 * Low-power mode also pauses decorative motion; leaving it refreshes the
 * current page and restores the normal 250 ms service cadence.
 */
void openvela_ui_sport_set_low_power(
    openvela_ui_sport_t *sport,
    bool low_power);

void openvela_ui_sport_set_event_cb(
    openvela_ui_sport_t *sport,
    openvela_ui_sport_event_cb_t callback,
    void *user_data);

/*
 * Submit a completed gesture in display pixels.  Returns true when consumed.
 * Page changes are queued with lv_async_call() and performed behind one opaque
 * transition surface.
 */
bool openvela_ui_sport_gesture(
    openvela_ui_sport_t *sport,
    int32_t delta_x,
    int32_t delta_y);

/* Lets the owner route its special-mode home gestures into this view. */
bool openvela_ui_sport_show_page(
    openvela_ui_sport_t *sport,
    openvela_ui_sport_page_t page);

/* Optional real sensor bridge.  Either argument may be zero to keep its value. */
void openvela_ui_sport_set_data(
    openvela_ui_sport_t *sport,
    uint32_t steps,
    uint16_t heart_rate);

void openvela_ui_sport_set_action_preview(
    openvela_ui_sport_t *sport,
    const char *action_preview_path);

void openvela_ui_sport_set_special_active(
    openvela_ui_sport_t *sport,
    bool active);

bool openvela_ui_sport_is_special_active(
    const openvela_ui_sport_t *sport);

bool openvela_ui_sport_is_transitioning(
    const openvela_ui_sport_t *sport);

openvela_ui_sport_page_t openvela_ui_sport_current_page(
    const openvela_ui_sport_t *sport);

lv_obj_t *openvela_ui_sport_root(openvela_ui_sport_t *sport);

#ifdef __cplusplus
}
#endif

#endif /* OPENVELA_UI_SPORT_H */
