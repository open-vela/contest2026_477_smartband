"""Exercise production animation callbacks and ownership across page changes."""
from test_architecture_workers import ROOT, run_c


def test_sport_motion_interpolation_and_lifecycle(tmp_path):
    source = (ROOT / 'firmware/openvela_ui/openvela_ui_sport.c').read_text()
    start = source.index('static void sport_apply_motion_frame(void *data, int32_t phase)\n{')
    end = source.index('static void sport_emit_event(', start)
    run_c(tmp_path, r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct {int x,y;uint32_t scale;} lv_obj_t;
typedef struct {
 bool destroying,visible,low_power,transitioning,confirm_visible,celebration_visible;
 int page,entry_cat_y,exit_cat_y,heart_x,heart_y;
 uint32_t heart_base_scale;
 lv_obj_t *entry_cat_visual,*exit_cat_visual,*heart_image,*heart_visual;
} openvela_ui_sport_t;
enum {OPENVELA_UI_SPORT_PAGE_ENTRY, OPENVELA_UI_SPORT_PAGE_EXIT,
      OPENVELA_UI_SPORT_PAGE_HEART_RATE, OTHER_PAGE};
typedef struct {void *var;void(*exec)(void*,int32_t);int from,to,duration,back,repeat;} lv_anim_t;
static lv_anim_t active;static unsigned starts;
#define LV_ANIM_REPEAT_INFINITE -1
static int sport_sx(openvela_ui_sport_t *s,int x){(void)s;return x;}
static bool lv_obj_is_valid(lv_obj_t *o){return o!=NULL;}
static void lv_obj_set_y(lv_obj_t *o,int y){o->y=y;}
static void lv_obj_set_pos(lv_obj_t *o,int x,int y){o->x=x;o->y=y;}
static void lv_image_set_scale(lv_obj_t *o,uint32_t s){o->scale=s;}
static void lv_anim_delete(void *v,void(*cb)(void*,int32_t)){
 if(active.var==v && (!cb || active.exec==cb))active.var=NULL;
}
static void lv_anim_init(lv_anim_t *a){*a=(lv_anim_t){0};}
static void lv_anim_set_var(lv_anim_t *a,void *v){a->var=v;}
static void lv_anim_set_exec_cb(lv_anim_t *a,void(*cb)(void*,int32_t)){a->exec=cb;}
static void lv_anim_set_values(lv_anim_t *a,int f,int t){a->from=f;a->to=t;}
static void lv_anim_set_duration(lv_anim_t *a,int d){a->duration=d;}
static void lv_anim_set_playback_duration(lv_anim_t *a,int d){a->back=d;}
static void lv_anim_set_repeat_count(lv_anim_t *a,int r){a->repeat=r;}
static void lv_anim_path_ease_in_out(void){}
static void lv_anim_set_path_cb(lv_anim_t *a,void(*cb)(void)){(void)a;(void)cb;}
static void lv_anim_start(lv_anim_t *a){active=*a;starts++;a->exec(a->var,a->from);}
''' + source[start:end] + r'''
int main(void){
 lv_obj_t cat={0},heart={0};
 openvela_ui_sport_t s={.visible=true,.entry_cat_y=50,.entry_cat_visual=&cat,
  .heart_image=&heart,.heart_base_scale=200,.heart_x=100,.heart_y=60};
 sport_start_page_animation(&s);assert(active.var==&s && cat.y==48);
 assert(active.duration==700 && active.back==700 && active.repeat==-1);
 active.exec(&s,500);assert(cat.y==50);active.exec(&s,1000);assert(cat.y==52);
 s.page=OPENVELA_UI_SPORT_PAGE_HEART_RATE;sport_start_page_animation(&s);
 assert(heart.scale==192);active.exec(&s,500);assert(heart.scale==201);
 active.exec(&s,1000);assert(heart.scale==210 && heart.x==95 && heart.y==55);
 s.low_power=true;active.exec(&s,0);assert(heart.scale==210);
 sport_stop_page_animations(&s);assert(!active.var && heart.scale==200 && cat.y==50);
 unsigned before=starts;sport_start_page_animation(&s);assert(starts==before);
 s.low_power=false;s.visible=false;sport_start_page_animation(&s);assert(starts==before);
 s.visible=true;s.page=OTHER_PAGE;sport_start_page_animation(&s);assert(starts==before);
 s.page=OPENVELA_UI_SPORT_PAGE_ENTRY;sport_start_page_animation(&s);
 s.confirm_visible=true;active.exec(&s,1000);assert(cat.y==48);
 sport_stop_page_animations(&s);assert(!active.var);
 s.confirm_visible=false;s.destroying=true;before=starts;
 sport_start_page_animation(&s);assert(starts==before && !active.var);
 return 0;
}
''')


def test_health_pulse_stop_handles_deleted_overlay(tmp_path):
    source = (ROOT / 'firmware/openvela_ui/openvela_ui.c').read_text()
    start = source.index('static void health_pulse_scale(')
    end = source.index('static void finish_health_measurement(', start)
    run_c(tmp_path, r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef struct {bool valid;uint32_t scale;} lv_obj_t;
static struct {lv_obj_t *health_heart_icon;void *health_measure_timer;
 bool health_measuring;int health_measure_ticks;uint32_t health_heart_scale;} g_ui;
static unsigned deleted,stopped;
static bool lv_obj_is_valid(lv_obj_t *o){return o->valid;}
static void lv_image_set_scale(void *p,uint32_t s){lv_obj_t *o=p;assert(o->valid);o->scale=s;}
static void lv_anim_delete(void *o,void(*cb)(void*,int32_t)){(void)o;(void)cb;stopped++;}
static void lv_timer_delete(void *t){(void)t;deleted++;}
''' + source[start:end] + r'''
int main(void){
 lv_obj_t icon={true,100};g_ui.health_heart_icon=&icon;g_ui.health_heart_scale=200;
 g_ui.health_measure_timer=&icon;g_ui.health_measuring=true;g_ui.health_measure_ticks=3;
 health_pulse_scale(&icon,210);assert(icon.scale==210);
 stop_health_measurement();assert(icon.scale==200 && stopped==1 && deleted==1);
 assert(!g_ui.health_measure_timer && !g_ui.health_measuring && !g_ui.health_measure_ticks);
 icon.valid=false;stop_health_measurement();assert(stopped==2 && deleted==1);
 g_ui.health_heart_icon=NULL;stop_health_measurement();assert(stopped==2);
 return 0;
}
''')
