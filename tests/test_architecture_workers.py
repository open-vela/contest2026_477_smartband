"""Compile the production queue/storage/rectangle paths with fault injection."""
from pathlib import Path
import shutil
import subprocess
import pytest

ROOT = Path(__file__).resolve().parents[1]


def run_c(tmp_path, source, extra=()):
    cc = shutil.which("gcc")
    if not cc:
        pytest.skip("POSIX C compiler required")
    file = tmp_path / "test.c"
    file.write_text(source)
    binary = tmp_path / "test"
    result = subprocess.run([cc, "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-pthread",
                             "-I", str(ROOT / "firmware/lcd"),
                             "-I", str(ROOT / "firmware/openvela_ui"),
                             str(file), *map(str, extra), "-o", str(binary)], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
    assert result.returncode == 0, result.stdout + result.stderr


def test_rectangle_stride_limits_and_errors(tmp_path):
    run_c(tmp_path, r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#define FAR
#define ILI9341_COLUMN_ADDRESS_SET 0x2a
#define ILI9341_PAGE_ADDRESS_SET 0x2b
#define ILI9341_MEMORY_WRITE 0x2c
typedef uint16_t fb_coord_t;
struct lcd_dev_s {int dummy;};
struct ili9341_lcd_s {
 void (*select)(struct ili9341_lcd_s *);
 void (*deselect)(struct ili9341_lcd_s *);
 int (*sendcmd)(struct ili9341_lcd_s *,uint8_t);
 int (*sendparam)(struct ili9341_lcd_s *,uint8_t);
 int (*sendgram)(struct ili9341_lcd_s *,const uint16_t *,uint32_t);
};
struct ili9341_dev_s {struct lcd_dev_s base;struct ili9341_lcd_s *lcd;};
static int ili9341_getxres(struct ili9341_dev_s *d){(void)d;return 320;}
static int ili9341_getyres(struct ili9341_dev_s *d){(void)d;return 240;}
static unsigned calls,fail_at,selected,released,grams,used,nbytes;
static uint16_t pixels[76800];static uint8_t bytes[32];
static void select_lcd(struct ili9341_lcd_s *d){(void)d;selected++;}
static void deselect_lcd(struct ili9341_lcd_s *d){(void)d;released++;}
static int byte(struct ili9341_lcd_s *d,uint8_t v){(void)d;bytes[nbytes++]=v;return ++calls==fail_at?-EIO:0;}
static int gram(struct ili9341_lcd_s *d,const uint16_t *p,uint32_t n){
 (void)d;grams++;assert(n<=2048);if(++calls==fail_at)return -EIO;
 memcpy(pixels+used,p,n*2);used+=n;return 0;
}
#include "ili9341_area.h"
static void reset(void){calls=selected=released=grams=used=nbytes=fail_at=0;}
int main(void){
 struct ili9341_lcd_s lcd={select_lcd,deselect_lcd,byte,byte,gram};
 struct ili9341_dev_s dev={{0},&lcd};
 uint16_t padded[]={1,2,3,0xbeef,4,5,6,0xbeef};
 assert(!ili9341_putarea(&dev.base,7,8,11,13,(uint8_t*)padded,8));
 uint16_t expected[]={1,2,3,4,5,6};uint8_t commands[]={0x2a,0,11,0,13,0x2b,0,7,0,8,0x2c};
 assert(selected==1 && released==1 && grams==2 && used==6);
 assert(!memcmp(pixels,expected,sizeof(expected)) && !memcmp(bytes,commands,sizeof(commands)));
 reset();assert(ili9341_putarea(&dev.base,0,1,0,2,(uint8_t*)padded,4)==-EINVAL);
 assert(ili9341_putarea(&dev.base,0,240,0,2,(uint8_t*)padded,8)==-EINVAL);
 assert(ili9341_putarea(&dev.base,1,0,0,2,(uint8_t*)padded,8)==-EINVAL);
 assert(ili9341_putarea(&dev.base,0,1,0,320,(uint8_t*)padded,8)==-EINVAL);
 assert(ili9341_putarea(&dev.base,0,1,0,2,(uint8_t*)padded+1,8)==-EINVAL);
 assert(!selected);
 static uint16_t full[320*120];for(unsigned i=0;i<320*120;i++)full[i]=i;
 assert(!ili9341_putarea(&dev.base,0,119,0,319,(uint8_t*)full,640));
 assert(grams==19 && nbytes==11 && used==320*120 && !memcmp(pixels,full,sizeof(full)));
 for(unsigned i=1;i<=30;i++){
  reset();fail_at=i;assert(ili9341_putarea(&dev.base,0,119,0,319,(uint8_t*)full,640)==-EIO);
  assert(calls==i && selected==1 && released==1);
 }
 return 0;
}
''')


def test_health_queue_retains_failed_head_and_rejects_overflow(tmp_path):
    run_c(tmp_path, r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <syslog.h>
#define SYNC_HEALTH_QUEUE_SIZE 4
#define SYNC_DAY_SIZE 9
struct sync_health_event_s {time_t measured_at;int primary;};
static struct {pthread_mutex_t lock;pthread_cond_t condition;bool started,stopping;
 struct sync_health_event_s queue[4];unsigned queue_head,queue_count,rejected,persist_failures,health_backoff;
 uint64_t health_retry_at;
} g_sync={.lock=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER,.started=true};
static unsigned calls,count;static int fail;static struct sync_health_event_s written[8];
static bool sync_day_for_time(time_t t,char *day){(void)day;return t>=1000;}
static int sync_persist_health(const struct sync_health_event_s *e){calls++;if(fail)return -1;written[count++]=*e;return 0;}
#include "openvela_ui_health_queue.h"
int main(void){
 for(int i=0;i<5;i++){struct sync_health_event_s e={1000,i};sync_queue_health(&e);}
 assert(g_sync.queue_count==4 && g_sync.rejected==1);
 fail=1;assert(!sync_flush_health(1000,0));assert(calls==1 && g_sync.queue_count==4);
 assert(!sync_flush_health(1000,0) && calls==1);
 fail=0;assert(sync_flush_health(1000,1));assert(count==4 && !g_sync.queue_count);
 for(int i=0;i<4;i++)assert(written[i].primary==i);
 struct sync_health_event_s e={0,42};sync_queue_health(&e);
 assert(!sync_flush_health(0,2) && calls==5);
 fail=1;assert(!sync_flush_health(1200,3));assert(g_sync.queue[g_sync.queue_head].measured_at==1200);
 fail=0;assert(sync_flush_health(1300,4));assert(written[4].measured_at==1200);
 g_sync.stopping=true;sync_queue_health(&e);assert(g_sync.rejected==2 && !g_sync.queue_count);
 return 0;
}
''')


def test_storage_coalesces_snapshots_and_retries_failures(tmp_path):
    run_c(tmp_path, r'''
#include <assert.h>
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include "openvela_ui_storage.h"
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake=PTHREAD_COND_INITIALIZER;
static int entered,release_first,last_value,calls;
static int write_snapshot(void *data){
 int v=*(int*)data;calls++;
 if(v==1){pthread_mutex_lock(&lock);entered=1;pthread_cond_signal(&wake);
 while(!release_first){pthread_cond_wait(&wake,&lock);}pthread_mutex_unlock(&lock);return -EIO;}
 if(v==4)return -EIO;
 last_value=v;return 0;
}
static void submit(int v){int *p=malloc(sizeof(*p));*p=v;assert(!openvela_ui_storage_submit(p,write_snapshot));}
int main(void){
 submit(1);pthread_mutex_lock(&lock);while(!entered)pthread_cond_wait(&wake,&lock);pthread_mutex_unlock(&lock);
 submit(2);submit(3); /* Both return while the writer is still blocked. */
 pthread_mutex_lock(&lock);release_first=1;pthread_cond_signal(&wake);pthread_mutex_unlock(&lock);
 assert(!openvela_ui_storage_stop());assert(last_value==3 && calls==2);
 submit(4);assert(openvela_ui_storage_stop()==-EIO);assert(last_value==3);
 submit(5);assert(!openvela_ui_storage_stop());assert(last_value==5);
 return 0;
}
''', [ROOT / "firmware/openvela_ui/openvela_ui_storage.c"])


@pytest.mark.parametrize("buffers", [0, 2])
def test_lcd_allocation_alignment_and_buffer_ownership(tmp_path, buffers):
    # Compile the actual lifecycle functions against LVGL's embedded descriptor contract.
    source = (ROOT / "firmware/lvgl/lv_nuttx_lcd.c").read_text()
    def function(signature):
        start = source.rindex(signature)
        opening = source.index("{", start)
        depth = 1
        end = opening + 1
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        return source[start:end]
    descriptor = source[source.index("typedef struct {"):source.index("} lv_nuttx_lcd_t;") + len("} lv_nuttx_lcd_t;")]
    stub = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#define LV_NUTTX_LCD_BUFFER_SIZE 120
#define LV_DISPLAY_RENDER_MODE_FULL 0
#define LV_DISPLAY_RENDER_MODE_PARTIAL 1
#define LV_EVENT_INVALIDATE_AREA 1
#define LV_EVENT_RENDER_START 2
#define LV_EVENT_RENDER_READY 3
#define LV_EVENT_DELETE 4
#define LCDDEVIO_GETAREAALIGN 5
#define LV_ASSERT_MALLOC(x) ((void)0)
#define LV_LOG_ERROR(...) ((void)0)
#define LV_LOG_USER(...) ((void)0)
typedef uint16_t lv_color_t;
typedef int lv_display_render_mode_t;
typedef struct {void *data;} lv_draw_buf_t;
typedef struct {void *user_data;} lv_event_t;
typedef struct {int unused;} lv_area_t;
typedef struct display {
 void *driver_data;lv_draw_buf_t *buf_1,*buf_2,*buf_act;
 lv_draw_buf_t _static_buf1,_static_buf2;
 void (*release)(lv_event_t*);void *release_data;
} lv_display_t;
struct lcddev_area_s {int unused;};
struct lcddev_area_align_s {uint16_t col_start_align,row_start_align,width_align,height_align;};
static struct {unsigned buffer_bytes,buffer_rows,render_mode;} g_lv_nuttx_lcd_perf_stats;
static void *allocations[16];static int allocations_count,attempt,fail_at,closes,io_mode;
static void *allocate(size_t size){
 if(++attempt==fail_at)return NULL;
 void *p=calloc(1,size);assert(p);allocations[allocations_count++]=p;return p;
}
static void lv_free(void *p){
 if(!p)return;
 int found=-1;for(int i=0;i<allocations_count;i++)if(allocations[i]==p)found=i;
 assert(found>=0); /* Reject freeing a draw descriptor embedded in the display. */
 allocations[found]=allocations[--allocations_count];free(p);
}
#define lv_malloc allocate
#define lv_malloc_zeroed allocate
static lv_display_t *lv_display_create(int w,int h){(void)w;(void)h;return allocate(sizeof(lv_display_t));}
static void lv_display_delete(lv_display_t *d){
 if(d->release){lv_event_t e={d->release_data};d->release(&e);}lv_free(d);
}
static int lv_display_get_color_format(lv_display_t *d){(void)d;return 1;}
static int lv_color_format_get_size(int format){(void)format;return 2;}
static void lv_display_set_buffers(lv_display_t *d,void *a,void *b,unsigned bytes,int mode){
 (void)bytes;(void)mode;d->_static_buf1.data=a;d->_static_buf2.data=b;
 d->buf_1=&d->_static_buf1;d->buf_2=b?&d->_static_buf2:NULL;d->buf_act=d->buf_1;
}
static void flush_cb(lv_display_t *d,const lv_area_t *a,uint8_t *p){(void)d;(void)a;(void)p;}
static void rounder_cb(lv_event_t *e){(void)e;}
static void render_timing_cb(lv_event_t *e){(void)e;}
static void lv_display_set_flush_cb(lv_display_t *d,void (*cb)(lv_display_t*,const lv_area_t*,uint8_t*)){(void)d;(void)cb;}
static void lv_display_add_event_cb(lv_display_t *d,void (*cb)(lv_event_t*),int event,void *data){if(event==LV_EVENT_DELETE){d->release=cb;d->release_data=data;}}
static void lv_display_set_driver_data(lv_display_t *d,void *p){d->driver_data=p;}
static void *lv_display_get_driver_data(lv_display_t *d){return d->driver_data;}
static void *lv_event_get_user_data(lv_event_t *e){return e->user_data;}
static int ioctl(int fd,int command,void *data){
 (void)fd;(void)command;struct lcddev_area_align_s *a=data;
 if(io_mode==1){errno=ENOTTY;return -1;}
 a->col_start_align=a->row_start_align=a->height_align=1;a->width_align=io_mode==2?3:1;return 0;
}
static int close(int fd){assert(fd==7);closes++;return 0;}
static void display_release_cb(lv_event_t *e);
'''
    main = r'''
int main(void){
 for(int fail=1;fail<=(LV_NUTTX_LCD_BUFFER_COUNT==2?4:3);fail++){
  fail_at=fail;attempt=closes=0;assert(!lcd_init(7,320,240));assert(!allocations_count && !closes);
 }
 fail_at=0;
 for(int mode=0;mode<3;mode++){
  io_mode=mode;closes=0;lv_display_t *d=lcd_init(7,320,240);
  if(mode==2){assert(!d && !allocations_count && !closes);continue;}
  assert(d);lv_display_delete(d);assert(!allocations_count && closes==1);
 }
 return 0;
}
'''
    run_c(tmp_path, f"#define LV_NUTTX_LCD_BUFFER_COUNT {buffers}\n" + stub + descriptor + "\n" +
          function("static lv_display_t * lcd_init(") + "\n" +
          function("static void display_release_cb(") + main)
