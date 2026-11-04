#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define LV_USE_DRAW_G2D 1
#define LV_OPA_MIN 2
#define LV_OPA_MAX 253
#define LV_LOG_WARN(...) ((void)0)
#define lv_memset memset
#define G2D_PIXEL_ALPHA 0
#define G2D_CMD_FILLRECT_H 1
/* Public Allwinner format enum values. */
#define G2D_FORMAT_ARGB8888 0
#define G2D_FORMAT_RGB888 8
#define G2D_FORMAT_RGB565 10
typedef uint8_t lv_opa_t;
typedef unsigned lv_color_format_t;
typedef struct {int32_t x1,y1,x2,y2;} lv_area_t;
typedef struct {uint8_t *data;struct {unsigned stride,w,h;} header;} lv_draw_buf_t;
typedef struct {lv_area_t buf_area;lv_draw_buf_t *draw_buf;lv_color_format_t color_format;} lv_layer_t;
typedef struct {lv_layer_t *target_layer;const lv_area_t *clip_area;} lv_draw_unit_t;
typedef struct {lv_opa_t opa;uint32_t color;} lv_draw_fill_dsc_t;
typedef struct {
    unsigned alpha,mode,color,format;
    struct {int x,y,w,h;} clip_rect;
    unsigned width,height,align[3];uintptr_t laddr[3];unsigned use_phy_addr;
} g2d_image;
typedef struct {g2d_image dst_image_h;} g2d_fillrect_h;
static int32_t lv_area_get_width(const lv_area_t *a){return a->x2-a->x1+1;}
static int32_t lv_area_get_height(const lv_area_t *a){return a->y2-a->y1+1;}
static void lv_area_copy(lv_area_t *a,const lv_area_t *b){*a=*b;}
static void lv_area_move(lv_area_t *a,int x,int y){a->x1+=x;a->x2+=x;a->y1+=y;a->y2+=y;}
static bool _lv_area_intersect(lv_area_t *o,const lv_area_t *a,const lv_area_t *b){
    *o=(lv_area_t){a->x1>b->x1?a->x1:b->x1,a->y1>b->y1?a->y1:b->y1,
                  a->x2<b->x2?a->x2:b->x2,a->y2<b->y2?a->y2:b->y2};
    return o->x1<=o->x2 && o->y1<=o->y2;
}
static unsigned lv_color_format_get_size(lv_color_format_t cf){return cf;}
static unsigned lv_draw_buf_width_to_stride(unsigned w,lv_color_format_t cf){return (w*cf+15)&~15U;}
static uint32_t lv_color_to_u32(uint32_t c){return c;}
static unsigned g2d_get_px_format(lv_color_format_t cf){
    return cf==2?G2D_FORMAT_RGB565:cf==3?G2D_FORMAT_RGB888:G2D_FORMAT_ARGB8888;
}
static uint8_t storage[320*240*4+128],expected[sizeof(storage)];
static uintptr_t buffer_start,buffer_end;
static unsigned calls,fallbacks;
static void hal_dcache_clean_invalidate(unsigned long start,unsigned long len){assert(start>=buffer_start && start+len<=buffer_end);}
static void lv_draw_sw_fill(lv_draw_unit_t *u,const lv_draw_fill_dsc_t *d,const lv_area_t *a){(void)u;(void)d;(void)a;fallbacks++;}
static int sunxi_g2d_control(int cmd,void *arg){
    assert(cmd==G2D_CMD_FILLRECT_H);calls++;
    g2d_image *im=&((g2d_fillrect_h*)arg)->dst_image_h;
    unsigned bpp=im->format==G2D_FORMAT_RGB565?2:im->format==G2D_FORMAT_RGB888?3:4;
    unsigned pitch=im->width*bpp;
    for(int y=0;y<im->clip_rect.h;y++){
        uintptr_t row=im->laddr[0]+(im->clip_rect.y+y)*pitch+im->clip_rect.x*bpp;
        assert(row>=buffer_start && row+(uintptr_t)im->clip_rect.w*bpp<=buffer_end);
        memset((void*)row,0x6c,im->clip_rect.w*bpp);
    }
    return 0;
}
#include "fill.c"
int main(void){
    /* Nonzero tile origin and padded pitch, then a second partial-refresh band. */
    const lv_area_t buffers[]={{60,1,261,184},{0,120,319,239},{51,31,251,130}};
    for(unsigned cf=2;cf<=4;cf++)for(unsigned b=0;b<3;b++)for(unsigned alpha=128;alpha<=255;alpha+=127){
        lv_area_t bounds=buffers[b];
        unsigned pitch=lv_draw_buf_width_to_stride(lv_area_get_width(&bounds),cf);
        unsigned bytes=pitch*lv_area_get_height(&bounds);
        memset(storage,0xa5,sizeof(storage));memcpy(expected,storage,sizeof(storage));
        buffer_start=(uintptr_t)storage+64;buffer_end=buffer_start+bytes;
        lv_draw_buf_t buf={(uint8_t*)buffer_start,{pitch,lv_area_get_width(&bounds),lv_area_get_height(&bounds)}};
        lv_layer_t layer={bounds,&buf,cf};
        lv_area_t clip=bounds;clip.x1+=3;clip.y1+=5;
        lv_draw_unit_t unit={&layer,&clip};lv_draw_fill_dsc_t fill={alpha,0x807060};
        calls=fallbacks=0;lv_draw_g2d_fill(&unit,&fill,&bounds);
        if(pitch%cf){assert(fallbacks==1 && calls==0);continue;}
        assert(calls==1 && !fallbacks);
        for(int y=clip.y1;y<=clip.y2;y++)
            memset(expected+64+(y-bounds.y1)*pitch+(clip.x1-bounds.x1)*cf,0x6c,lv_area_get_width(&clip)*cf);
        assert(!memcmp(storage,expected,sizeof(storage)));
        fill.opa=0;lv_draw_g2d_fill(&unit,&fill,&bounds);assert(calls==1);
        lv_area_t outside={-20,-20,-1,-1};fill.opa=255;
        lv_draw_g2d_fill(&unit,&fill,&outside);assert(calls==1);
    }
    puts("G2D bounded writes, format, padded pitch, origin, clip and guards: PASS");
    return 0;
}
