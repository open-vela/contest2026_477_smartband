#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LV_USE_DRAW_G2D 1
#define LV_OPA_MIN 2
#define LV_OPA_MAX 253
#define LV_SCALE_NONE 256
#define LV_STRIDE_AUTO 0
#define LV_RESULT_OK 0
#define LV_COLOR_FORMAT_ARGB8888 4
#define LV_COLOR_FORMAT_IS_NV(cf) ((cf)==9)
#define LV_LOG_WARN(...) ((void)0)
#define LV_LOG_INFO(...) ((void)0)
#define LV_LOG_ERROR(...) ((void)0)
#define lv_memset memset
#define G2D_PIXEL_ALPHA 0
#define G2D_GLOBAL_ALPHA 1
#define G2D_MIXER_ALPHA 2
#define G2D_BLT_NONE_H 0
#define G2D_BLD_SRCOVER 0
#define G2D_CMD_BITBLT_H 1
#define G2D_CMD_BLD_H 2
typedef uint8_t lv_opa_t;
typedef unsigned lv_color_format_t;
typedef int lv_result_t;
typedef struct {int32_t x,y;} lv_point_t;
typedef struct {int32_t x1,y1,x2,y2;} lv_area_t;
typedef struct {struct {unsigned cf,w,h,stride;} header;uint8_t *data;} lv_draw_buf_t;
typedef struct {lv_area_t buf_area;lv_draw_buf_t *draw_buf;} lv_layer_t;
typedef struct {lv_layer_t *target_layer;const lv_area_t *clip_area;} lv_draw_unit_t;
typedef struct {const void *src;lv_opa_t opa;int rotation,scale_x,scale_y;lv_point_t pivot;} lv_draw_image_dsc_t;
typedef struct {const lv_draw_buf_t *decoded;} lv_image_decoder_dsc_t;
typedef struct {struct {struct {unsigned stride;void *buf;} y,uv;} semi_planar;} lv_yuv_buf_t;
typedef struct {
    unsigned alpha,mode,color,format;
    struct {int x,y,w,h;} clip_rect;
    unsigned width,height,align[3];uintptr_t laddr[3];unsigned use_phy_addr;
} g2d_image;
typedef struct {unsigned flag_h;g2d_image src_image_h,dst_image_h;} g2d_blt_h;
typedef struct {unsigned bld_cmd;g2d_image src_image[2],dst_image;} g2d_bld;
static int32_t lv_area_get_width(const lv_area_t *a){return a->x2-a->x1+1;}
static int32_t lv_area_get_height(const lv_area_t *a){return a->y2-a->y1+1;}
static void lv_area_copy(lv_area_t *a,const lv_area_t *b){*a=*b;}
static void lv_area_move(lv_area_t *a,int x,int y){a->x1+=x;a->x2+=x;a->y1+=y;a->y2+=y;}
static bool _lv_area_is_equal(const lv_area_t *a,const lv_area_t *b){return !memcmp(a,b,sizeof(*a));}
static bool _lv_area_intersect(lv_area_t *o,const lv_area_t *a,const lv_area_t *b){
    *o=(lv_area_t){a->x1>b->x1?a->x1:b->x1,a->y1>b->y1?a->y1:b->y1,
                  a->x2<b->x2?a->x2:b->x2,a->y2<b->y2?a->y2:b->y2};
    return o->x1<=o->x2 && o->y1<=o->y2;
}
static unsigned lv_color_format_get_size(lv_color_format_t cf){return cf;}
static unsigned g2d_get_px_format(lv_color_format_t cf){return cf;}
static void _lv_image_buf_get_transformed_area(lv_area_t *a,int w,int h,int r,int sx,int sy,const lv_point_t *p){
    assert(!r);a->x1=p->x-p->x*sx/256;a->y1=p->y-p->y*sy/256;
    a->x2=a->x1+w*sx/256-1;a->y2=a->y1+h*sy/256-1;
}
static unsigned fallbacks,calls;
static void lv_draw_sw_image(lv_draw_unit_t *u,const lv_draw_image_dsc_t *d,const lv_area_t *a){(void)u;(void)d;(void)a;fallbacks++;}
static lv_result_t lv_image_decoder_open(lv_image_decoder_dsc_t *d,const void *src,void *args){(void)args;d->decoded=src;return 0;}
static void lv_image_decoder_close(lv_image_decoder_dsc_t *d){(void)d;}
static void lv_draw_buf_invalidate_cache(lv_draw_buf_t *b,const lv_area_t *a){(void)b;(void)a;}
static lv_draw_buf_t *allocations[8];
static void track(lv_draw_buf_t *b){for(unsigned i=0;i<8;i++)if(!allocations[i]){allocations[i]=b;return;}assert(0);}
static lv_draw_buf_t *lv_draw_buf_create(int w,int h,unsigned cf,unsigned stride){
    lv_draw_buf_t *b=calloc(1,sizeof(*b));b->header.cf=cf;b->header.w=w;b->header.h=h;
    b->header.stride=stride?stride:(w*cf+15)&~15U;b->data=calloc(h,b->header.stride);track(b);return b;
}
static void lv_draw_buf_destroy(lv_draw_buf_t *b){
    for(unsigned i=0;i<8;i++)if(allocations[i]==b)allocations[i]=NULL;
    free(b->data);free(b);
}
static void hal_dcache_clean_invalidate(unsigned long start,unsigned long len){
    for(unsigned i=0;i<8;i++)if(allocations[i]){
        lv_draw_buf_t *b=allocations[i];uintptr_t lo=(uintptr_t)b->data,hi=lo+b->header.stride*b->header.h;
        if(start>=lo && start<hi){assert(start+len<=hi);return;}
    }
    assert(0);
}
static void validate(const g2d_image *im){
    assert(im->clip_rect.x>=0 && im->clip_rect.y>=0 && im->clip_rect.w>0 && im->clip_rect.h>0);
    for(unsigned i=0;i<8;i++)if(allocations[i] && (uintptr_t)allocations[i]->data==im->laddr[0]){
        lv_draw_buf_t *b=allocations[i];
        assert(im->format==b->header.cf && im->width*im->format==b->header.stride);
        assert((unsigned)(im->clip_rect.x+im->clip_rect.w)<=b->header.w);
        assert((unsigned)(im->clip_rect.y+im->clip_rect.h)<=b->header.h);
        return;
    }
    assert(0);
}
static int sunxi_g2d_control(int cmd,void *arg){
    calls++;const g2d_image *src,*dst;
    if(cmd==G2D_CMD_BITBLT_H){g2d_blt_h *p=arg;src=&p->src_image_h;dst=&p->dst_image_h;}
    else {assert(cmd==G2D_CMD_BLD_H);g2d_bld *p=arg;src=&p->src_image[1];dst=&p->dst_image;validate(&p->src_image[0]);}
    validate(src);validate(dst);
    for(int y=0;y<dst->clip_rect.h;y++)for(int x=0;x<dst->clip_rect.w;x++){
        int sx=src->clip_rect.x+x*src->clip_rect.w/dst->clip_rect.w;
        int sy=src->clip_rect.y+y*src->clip_rect.h/dst->clip_rect.h;
        uint8_t value=((uint8_t*)src->laddr[0])[(sy*src->width+sx)*src->format];
        memset((uint8_t*)dst->laddr[0]+((y+dst->clip_rect.y)*dst->width+x+dst->clip_rect.x)*dst->format,value,dst->format);
    }
    return 0;
}
#include "image.c"
int main(void){
    for(unsigned cf=2;cf<=4;cf+=2)for(unsigned opacity=128;opacity<=255;opacity+=127)for(int scale=256;scale<=512;scale+=256){
        lv_draw_buf_t *src=lv_draw_buf_create(23,18,cf,0),*dst=lv_draw_buf_create(81,60,2,0);
        for(unsigned y=0;y<18;y++)for(unsigned x=0;x<23;x++)
            memset(src->data+y*src->header.stride+x*cf,1+x+23*y,cf);
        lv_layer_t layer={{60,120,140,179},dst};lv_area_t clip={65,125,140,179};
        lv_draw_unit_t unit={&layer,&clip};lv_area_t coords={62,123,84,140};
        lv_draw_image_dsc_t d={src,opacity,0,scale,scale,{0,0}};
        calls=0;lv_draw_g2d_img(&unit,&d,&coords);assert(calls && !fallbacks);
        /* Check every destination byte, including untouched padding. */
        for(unsigned y=0;y<60;y++)for(unsigned x=0;x<dst->header.stride;x++){
            unsigned px=x/2;uint8_t want=0;
            if(px>=5 && px<(unsigned)(2+23*scale/256) && y>=5 && y<(unsigned)(3+18*scale/256)){
                /* Exact nearest-neighbor value for unscaled/clipped and 2x alpha path. */
                if(scale==256 || cf==4 || opacity<253)
                    want=(uint8_t)(1+(px-2)*256/scale+23*((y-3)*256/scale));
                else continue; /* clipped hardware scaling resamples its cropped source */
            }
            assert(dst->data[y*dst->header.stride+x]==want);
        }
        lv_draw_buf_destroy(src);lv_draw_buf_destroy(dst);
    }
    puts("G2D image pitch, local origins, bounds, clipping and scaling: PASS");return 0;
}
