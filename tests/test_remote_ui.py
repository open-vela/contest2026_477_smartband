import pathlib
import shutil
import subprocess
import pytest


def test_snapshot_stride_and_pointer_lifecycle(tmp_path):
    cc = shutil.which('cc') or shutil.which('gcc')
    if not cc:
        pytest.skip('POSIX C compiler required for Native UI harness')
    (tmp_path/'nuttx').mkdir()
    (tmp_path/'nuttx/config.h').write_text('')
    (tmp_path/'lvgl').mkdir()
    (tmp_path/'lvgl/lvgl.h').write_text(r'''
#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <pthread.h>
typedef struct {int x,y;} lv_point_t;
typedef int lv_indev_state_t;
typedef struct {lv_point_t point;int state;bool continue_reading;} lv_indev_data_t;
typedef struct lv_indev {void (*read)(struct lv_indev*,lv_indev_data_t*);} lv_indev_t;
typedef struct lv_timer {void (*cb)(struct lv_timer*);} lv_timer_t;
typedef struct {struct {unsigned w,h,stride;} header;unsigned char *data;} lv_draw_buf_t;
typedef int lv_display_t;
static uint32_t ticks;
static pthread_t owner;
static lv_indev_data_t last_input;
static int samples;
static void ui(void){assert(pthread_equal(owner,pthread_self()));}
#define LV_INDEV_STATE_RELEASED 0
#define LV_INDEV_STATE_PRESSED 1
#define LV_INDEV_TYPE_POINTER 1
#define LV_INDEV_MODE_EVENT 1
#define LV_COLOR_FORMAT_RGB565 1
static uint32_t lv_tick_get(void){ui();return ticks;}
static void *lv_screen_active(void){ui();return (void*)1;}
static lv_draw_buf_t *lv_snapshot_take(void *screen,int cf){
 (void)screen;(void)cf;ui();
 static unsigned char pixels[]={0x00,0xf8,0xe0,0x07,0x5a,0x5a,0x1f,0x00,0xff,0xff,0x5a,0x5a};
 static lv_draw_buf_t b={{2,2,6},pixels};return &b;
}
static void lv_draw_buf_destroy(lv_draw_buf_t *b){(void)b;ui();}
static lv_display_t *lv_display_get_default(void){ui();return (void*)1;}
static int lv_display_get_horizontal_resolution(lv_display_t *d){(void)d;ui();return 320;}
static int lv_display_get_vertical_resolution(lv_display_t *d){(void)d;ui();return 240;}
static lv_indev_t *lv_indev_create(void){ui();return calloc(1,sizeof(lv_indev_t));}
static void lv_indev_delete(lv_indev_t *p){ui();free(p);}
static void lv_indev_set_type(lv_indev_t *p,int t){(void)p;(void)t;ui();}
static void lv_indev_set_display(lv_indev_t *p,lv_display_t *d){(void)p;(void)d;ui();}
static void lv_indev_set_mode(lv_indev_t *p,int m){(void)p;(void)m;ui();}
static void lv_indev_set_read_cb(lv_indev_t *p,void (*cb)(lv_indev_t*,lv_indev_data_t*)){ui();p->read=cb;}
static void lv_indev_read(lv_indev_t *p){ui();p->read(p,&last_input);samples++;}
static lv_timer_t *lv_timer_create(void (*cb)(lv_timer_t*),int period,void *data){
 (void)period;(void)data;ui();lv_timer_t *t=malloc(sizeof(*t));t->cb=cb;return t;
}
static void lv_timer_delete(lv_timer_t *t){ui();free(t);}
''')
    source = pathlib.Path(__file__).resolve().parents[1]/'firmware/openvela_ui/openvela_ui_remote.c'
    harness = tmp_path/'remote_test.c'
    harness.write_text(f'#define MOONCAT_CONTROL_DIR "{tmp_path.as_posix()}"\n#include "{source.as_posix()}"\n'+r'''
static void command(const char *s){FILE *f=fopen(REQUEST,"w");assert(f);fputs(s,f);fclose(f);control_tick(NULL);}
static void status(unsigned long id,int expected){unsigned long actual,tick;int code;FILE *f=fopen(RESPONSE,"r");assert(f);assert(fscanf(f,"%lu %d %lu",&actual,&code,&tick)==3);fclose(f);assert(actual==id && code==expected);}
int main(void){
 owner=pthread_self();ticks=1000;assert(openvela_ui_remote_start()==0);
 command("1 screenshot");status(1,0);
 FILE *f=fopen(FRAME,"rb");unsigned char content[64];size_t n=fread(content,1,sizeof(content),f);fclose(f);
 const unsigned char expected[]="P6\n2 2\n255\n\xff\0\0\0\xff\0\0\0\xff\xff\xff\xff";
 assert(n==sizeof(expected)-1 && memcmp(content,expected,n)==0);
 command("2 tap 20 30");assert(last_input.state==LV_INDEV_STATE_PRESSED);
 ticks+=100;control_tick(NULL);status(2,0);assert(last_input.state==LV_INDEV_STATE_RELEASED);
 command("3 swipe 10 20 110 120 200");ticks+=100;control_tick(NULL);
 assert(last_input.point.x==60 && last_input.point.y==70 && last_input.state==LV_INDEV_STATE_PRESSED);
 ticks+=100;control_tick(NULL);status(3,0);assert(last_input.point.x==110 && last_input.state==LV_INDEV_STATE_RELEASED);
 int before=samples;command("4 tap -1 0");status(4,-EINVAL);assert(samples==before);
 command("5 swipe 0 0 319 239 0");status(5,-EINVAL);
 command("6 unknown");status(6,-EINVAL);
 openvela_ui_remote_stop();assert(g_pointer==NULL && g_control_timer==NULL);
 return 0;
}
''')
    binary=tmp_path/'remote_test'
    result=subprocess.run([cc,'-std=gnu11','-pthread','-I',str(tmp_path),str(harness),'-o',str(binary)],capture_output=True)
    assert result.returncode==0,result.stderr.decode(errors='replace')
    result=subprocess.run([str(binary)],capture_output=True)
    assert result.returncode==0,result.stderr.decode(errors='replace')
