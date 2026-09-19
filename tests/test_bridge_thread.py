"""Exercise the real bridge with a worker-thread tap and a UI-thread timer."""
import pathlib
import shutil
import subprocess

import pytest


def test_agent_notification_stays_on_ui_thread(tmp_path):
    cc = shutil.which('cc') or shutil.which('gcc')
    if not cc:
        pytest.skip('POSIX C compiler required for pthread bridge harness')
    headers = {
        'nuttx/config.h': '#define CONFIG_EXAMPLES_AI_AGENT_VELA 1\n#define OK 0\n',
        'core/message_bus.h': '#pragma once\ntypedef struct { const char *content; } agent_msg_t;\n',
        'core/message_bus_tap.h': '''#include "message_bus.h"
typedef void (*tap_fn)(const agent_msg_t *, void *);
int mbus_tap_register(const char *, tap_fn, void *);
int mbus_tap_unregister(const char *);
''',
        'lvgl/lvgl.h': '''#pragma once
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
typedef struct { int unused; } lv_obj_t;
typedef struct lv_timer { void (*cb)(struct lv_timer *); } lv_timer_t;
static pthread_t ui_thread;
static int wrong_thread, rendered;
static lv_obj_t object;
static lv_timer_t *poll_timer;
static void ui_only(void) { if (!pthread_equal(pthread_self(), ui_thread)) wrong_thread++; }
static lv_timer_t *lv_timer_create(void (*cb)(lv_timer_t *), int ms, void *data) {
    (void)data; ui_only(); lv_timer_t *t=malloc(sizeof(*t)); t->cb=cb;
    if (ms != 8000) poll_timer=t;
    return t;
}
static void lv_timer_delete(lv_timer_t *t) { ui_only(); free(t); }
static lv_obj_t *lv_screen_active(void) { ui_only(); return &object; }
static lv_obj_t *lv_obj_create(lv_obj_t *p) { (void)p; ui_only(); return &object; }
static lv_obj_t *lv_label_create(lv_obj_t *p) { return lv_obj_create(p); }
static int lv_obj_is_valid(lv_obj_t *p) { ui_only(); return p != NULL; }
static void lv_label_set_text(lv_obj_t *p,const char *s) {
    (void)p; ui_only(); if(strcmp(s,"latest notification")==0) rendered++;
}
static int lv_async_call(void (*cb)(void *),void *data) { (void)cb;(void)data;ui_only();return 0; }
static int lv_async_call_cancel(void (*cb)(void *),void *data) { (void)cb;(void)data;ui_only();return 0; }
#define LV_RESULT_OK 0
#define lv_obj_delete(...) ui_only()
#define lv_obj_remove_style_all(...) ui_only()
#define lv_obj_set_size(...) ui_only()
#define lv_obj_align(...) ui_only()
#define lv_obj_set_style_bg_color(...) ui_only()
#define lv_obj_set_style_bg_opa(...) ui_only()
#define lv_obj_set_style_border_color(...) ui_only()
#define lv_obj_set_style_border_width(...) ui_only()
#define lv_obj_set_style_radius(...) ui_only()
#define lv_obj_set_style_pad_all(...) ui_only()
#define lv_obj_clear_flag(...) ui_only()
#define lv_obj_set_width(...) ui_only()
#define lv_obj_set_style_text_color(...) ui_only()
#define lv_obj_set_style_text_font(...) ui_only()
#define lv_label_set_long_mode(...) ui_only()
#define lv_obj_move_foreground(...) ui_only()
''',
    }
    for name, text in headers.items():
        dest = tmp_path / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(text, encoding='utf-8')
    bridge = pathlib.Path(__file__).resolve().parents[1] / 'firmware/openvela_ui/openvela_ui_agent_bridge.c'
    harness = tmp_path / 'bridge_test.c'
    harness.write_text('''#include <assert.h>
#include <stddef.h>
#include "core/message_bus_tap.h"
static tap_fn tap;
int mbus_tap_register(const char *name,tap_fn fn,void *cookie) {
    (void)name;(void)cookie;tap=fn;return 0;
}
int mbus_tap_unregister(const char *name) { (void)name;tap=NULL;return 0; }
''' + f'#include "{bridge.as_posix()}"\n' + '''
static void *worker(void *unused) {
    (void)unused;
    agent_msg_t a={"first notification"},b={"latest notification"};
    tap(&a,NULL);tap(&b,NULL);return NULL;
}
int main(void) {
    pthread_t thread;ui_thread=pthread_self();
    assert(openvela_ui_agent_bridge_start()==0);
    assert(pthread_create(&thread,NULL,worker,NULL)==0);
    pthread_join(thread,NULL);
    assert(wrong_thread==0);
    assert(rendered==0);
    assert(poll_timer != NULL);
    poll_timer->cb(poll_timer);
    assert(rendered==1);
    poll_timer->cb(poll_timer);
    assert(rendered==1);
    openvela_ui_agent_bridge_stop();
    assert(tap==NULL && wrong_thread==0);
    return 0;
}
''', encoding='utf-8')
    binary = tmp_path / 'bridge_test'
    subprocess.run([cc, '-std=gnu11', '-pthread', '-I', str(tmp_path), str(harness), '-o', str(binary)], check=True, capture_output=True)
    result = subprocess.run([str(binary)], capture_output=True)
    assert result.returncode == 0, result.stderr.decode(errors='replace')
