/* Minimal host LVGL contract double: checks every API stays on the UI thread.
 * This is not a renderer or an assertion about device touch/appearance. */
#pragma once
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef struct lv_obj {int valid;char text[320];struct lv_obj *parent;} lv_obj_t;
typedef struct {void *user_data;} lv_event_t;
typedef struct lv_timer {void (*cb)(struct lv_timer *);} lv_timer_t;
static pthread_t test_ui_thread;
static lv_obj_t test_objects[2000];
static unsigned test_object_count;
static void check_ui(void) {assert(pthread_equal(test_ui_thread,pthread_self()));}
static lv_obj_t *lv_layer_top(void) {check_ui();return &test_objects[0];}
static lv_obj_t *lv_obj_create(lv_obj_t *p) {check_ui();assert(++test_object_count<2000);lv_obj_t *o=&test_objects[test_object_count];o->valid=1;o->parent=p;return o;}
static lv_obj_t *lv_button_create(lv_obj_t *p) {return lv_obj_create(p);}
static lv_obj_t *lv_label_create(lv_obj_t *p) {return lv_obj_create(p);}
static void lv_label_set_text(lv_obj_t *o,const char *text) {check_ui();assert(o && o->valid);snprintf(o->text,sizeof(o->text),"%s",text);}
static int lv_obj_is_valid(lv_obj_t *o) {check_ui();return o && o->valid;}
static void lv_obj_delete(lv_obj_t *o) {check_ui();assert(o && o->valid);o->valid=0;}
static void *lv_event_get_user_data(lv_event_t *e) {check_ui();return e->user_data;}
static lv_timer_t *lv_timer_create(void (*cb)(lv_timer_t *),int ms,void *data) {check_ui();(void)ms;(void)data;lv_timer_t *t=malloc(sizeof(*t));t->cb=cb;return t;}
static void lv_timer_delete(lv_timer_t *t) {check_ui();free(t);}
static void lv_obj_add_event_cb(lv_obj_t *o,void (*cb)(lv_event_t *),int code,void *data) {check_ui();(void)o;(void)cb;(void)code;(void)data;}
#define LV_EVENT_CLICKED 1
#define lv_obj_set_size(...) check_ui()
#define lv_obj_set_pos(...) check_ui()
#define lv_obj_set_width(...) check_ui()
#define lv_obj_center(...) check_ui()
#define lv_obj_align(...) check_ui()
#define lv_obj_set_style_pad_all(...) check_ui()
#define lv_obj_clear_flag(...) check_ui()
