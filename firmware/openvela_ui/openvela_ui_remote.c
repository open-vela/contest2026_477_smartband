/* SPDX-License-Identifier: Apache-2.0 */
#include <nuttx/config.h>
#include <lvgl/lvgl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "openvela_ui_remote.h"

#ifndef MOONCAT_CONTROL_DIR
#define MOONCAT_CONTROL_DIR "/tmp"
#endif
#define REQUEST MOONCAT_CONTROL_DIR "/mooncat-control.req"
#define RESPONSE MOONCAT_CONTROL_DIR "/mooncat-control.status"
#define FRAME MOONCAT_CONTROL_DIR "/mooncat-screen.ppm"
#define POLL_MS 20

static lv_timer_t *g_control_timer;
static lv_indev_t *g_pointer;
static lv_point_t g_point;
static lv_indev_state_t g_state = LV_INDEV_STATE_RELEASED;
static bool g_gesture;
static int g_x0, g_y0, g_x1, g_y1;
static uint32_t g_started, g_duration;
static unsigned long g_request_id;

static void reply(unsigned long id, int result)
{
    FILE *file = fopen(RESPONSE ".part", "w");
    if (file) {
        int ok = fprintf(file, "%lu %d %lu\n", id, result,
                         (unsigned long)lv_tick_get()) > 0;
        int closed = fclose(file);
        if (ok && closed == 0) rename(RESPONSE ".part", RESPONSE);
    }
}

static int screenshot(void)
{
    lv_draw_buf_t *image = lv_snapshot_take(lv_screen_active(),
                                           LV_COLOR_FORMAT_RGB565);
    if (!image) return -ENOMEM;
    unsigned width = image->header.w;
    unsigned height = image->header.h;
    unsigned stride = image->header.stride;
    unsigned char *row = malloc(width * 3);
    FILE *file = row ? fopen(FRAME ".part", "wb") : NULL;
    int result = -EIO;
    if (file && fprintf(file, "P6\n%u %u\n255\n", width, height) > 0) {
        result = 0;
        for (unsigned y = 0; y < height; y++) {
            const unsigned char *src = image->data + y * stride;
            for (unsigned x = 0; x < width; x++) {
                uint16_t pixel;
                memcpy(&pixel, src + x * 2, sizeof(pixel));
                unsigned r = (pixel >> 11) & 31;
                unsigned g = (pixel >> 5) & 63;
                unsigned b = pixel & 31;
                row[x * 3] = (r << 3) | (r >> 2);
                row[x * 3 + 1] = (g << 2) | (g >> 4);
                row[x * 3 + 2] = (b << 3) | (b >> 2);
            }
            if (fwrite(row, 3, width, file) != width) {
                result = -EIO;
                break;
            }
        }
    }
    if (file && fclose(file) != 0) result = -EIO;
    free(row);
    lv_draw_buf_destroy(image);
    if (result == 0 && rename(FRAME ".part", FRAME) != 0) result = -errno;
    if (result != 0) unlink(FRAME ".part");
    return result;
}

static void pointer_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    data->point = g_point;
    data->state = g_state;
    data->continue_reading = false;
}

static bool valid_point(int x, int y)
{
    lv_display_t *display = lv_display_get_default();
    return x >= 0 && y >= 0 && x < lv_display_get_horizontal_resolution(display)
           && y < lv_display_get_vertical_resolution(display);
}

static void control_tick(lv_timer_t *timer)
{
    (void)timer;
    if (g_gesture) {
        uint32_t elapsed = lv_tick_get() - g_started;
        if (elapsed >= g_duration) elapsed = g_duration;
        g_point.x = g_x0 + (g_x1 - g_x0) * (int)elapsed / (int)g_duration;
        g_point.y = g_y0 + (g_y1 - g_y0) * (int)elapsed / (int)g_duration;
        if (elapsed == g_duration) {
            g_state = LV_INDEV_STATE_RELEASED;
            g_gesture = false;
        }
        lv_indev_read(g_pointer);
        if (!g_gesture) reply(g_request_id, 0);
    }

    FILE *file = fopen(REQUEST, "r");
    if (!file) return;
    char line[128], command[16], extra;
    unsigned long id = 0;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0, duration = 0;
    bool loaded = fgets(line, sizeof(line), file) != NULL;
    fclose(file);
    unlink(REQUEST);
    if (!loaded) return;
    int fields = sscanf(line, "%lu %15s %d %d %d %d %d %c", &id, command,
                        &x0, &y0, &x1, &y1, &duration, &extra);
    if (g_gesture) { reply(id, -EBUSY); return; }
    if (fields == 2 && strcmp(command, "screenshot") == 0) {
        reply(id, screenshot());
        return;
    }
    if (fields == 4 && strcmp(command, "tap") == 0) {
        x1 = x0; y1 = y0; duration = 100;
    } else if (fields != 7 || strcmp(command, "swipe") != 0) {
        reply(id, -EINVAL); return;
    }
    if (!valid_point(x0, y0) || !valid_point(x1, y1) ||
        duration < 20 || duration > 2000) {
        reply(id, -EINVAL); return;
    }
    g_x0 = x0; g_y0 = y0; g_x1 = x1; g_y1 = y1;
    g_duration = (uint32_t)duration;
    g_started = lv_tick_get();
    g_request_id = id;
    g_point.x = x0; g_point.y = y0;
    g_state = LV_INDEV_STATE_PRESSED;
    g_gesture = true;
    lv_indev_read(g_pointer);
}

int openvela_ui_remote_start(void)
{
    g_pointer = lv_indev_create();
    if (!g_pointer) return -ENOMEM;
    lv_indev_set_type(g_pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(g_pointer, lv_display_get_default());
    lv_indev_set_read_cb(g_pointer, pointer_read);
    lv_indev_set_mode(g_pointer, LV_INDEV_MODE_EVENT);
    unlink(REQUEST);
    g_control_timer = lv_timer_create(control_tick, POLL_MS, NULL);
    if (!g_control_timer) {
        lv_indev_delete(g_pointer); g_pointer = NULL;
        return -ENOMEM;
    }
    return 0;
}

void openvela_ui_remote_stop(void)
{
    if (g_control_timer) lv_timer_delete(g_control_timer);
    g_control_timer = NULL;
    if (g_pointer) lv_indev_delete(g_pointer);
    g_pointer = NULL;
    g_gesture = false;
    g_state = LV_INDEV_STATE_RELEASED;
}
