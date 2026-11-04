"""Reproduce LVGL restoring the boundary direction after VALUE_CHANGED."""
from test_architecture_workers import ROOT, run_c


def test_wrap_normalization_runs_after_framework_callback(tmp_path):
    source = (ROOT / 'firmware/openvela_ui/openvela_ui.c').read_text()
    start = source.index('static void normalize_tile_async(')
    end = source.index('int openvela_ui_status(', start)
    run_c(tmp_path, r'''
#include <assert.h>
#include <stddef.h>
#define UI_TILE_COUNT 8
#define UI_TILE_FIRST_REAL 1
#define UI_TILE_LAST_REAL 6
#define UI_PAGE_HOME 0
#define UI_PAGE_NOTIFICATIONS 5
#define LV_ANIM_OFF 0
#define LV_UNUSED(x) (void)(x)
typedef struct {int index;} lv_obj_t;
typedef struct {lv_obj_t *target;} lv_event_t;
static lv_obj_t tiles[8],view;
static struct {lv_obj_t *tileview,*tile[8];int current_page;} g_ui;
static const int g_tile_page[]={5,0,1,2,3,4,5,0};
static lv_obj_t *active;static unsigned direction,queued;
static void(*async_cb)(void*);static void *async_data;
static lv_obj_t *lv_event_get_current_target(lv_event_t *e){return e->target;}
static lv_obj_t *lv_tileview_get_tile_active(lv_obj_t *v){assert(v==&view);return active;}
static void lv_tileview_set_tile(lv_obj_t *v,lv_obj_t *tile,int anim){
 assert(v==&view && anim==0);active=tile;direction=tile->index==0?2:tile->index==7?1:3;
}
static int tile_index_from_obj(lv_obj_t *t){return t->index;}
static void update_page_dots(void){}
static int lv_async_call(void(*cb)(void*),void *data){async_cb=cb;async_data=data;queued++;return 0;}
''' + source[start:end] + r'''
static void wrap(int boundary,int expected,int page){
 lv_event_t event={&view};active=&tiles[boundary];queued=0;
 tileview_changed_cb(&event);assert(queued==1 && active==&tiles[boundary]);
 /* This is the write in LVGL's tileview_event_cb after our callback returns. */
 direction=boundary==0?2:1;
 async_cb(async_data);assert(active==&tiles[expected] && direction==3);
 assert(g_ui.current_page==page);
}
int main(void){
 g_ui.tileview=&view;for(int i=0;i<8;i++){tiles[i].index=i;g_ui.tile[i]=&tiles[i];}
 for(int repeat=0;repeat<10;repeat++){wrap(7,1,0);wrap(0,6,5);}
 lv_event_t event={&view};active=&tiles[2];queued=0;tileview_changed_cb(&event);
 assert(!queued && active==&tiles[2] && g_ui.current_page==1);
 return 0;
}
''')
