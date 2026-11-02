"""Pixel equivalence, timing boundaries and bounded cache lifetime."""
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]


def test_actual_animation_kernel(tmp_path):
    cc = shutil.which('gcc') or shutil.which('clang')
    if not cc:
        pytest.skip('C compiler required')
    source = tmp_path/'animation_test.c'
    source.write_text(r'''
#include "openvela_ui_animation.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
static uint32_t reference(uint32_t a, uint32_t b, unsigned t) {
    uint32_t out = 0;
    for (unsigned s=0; s<32; s+=8) {
        unsigned v = ((a>>s)&255)*(255-t) + ((b>>s)&255)*t + 128;
        out |= ((v+(v>>8))>>8)<<s;
    }
    return out;
}
static unsigned live, calls;
static void *tracked_alloc(size_t n) { void *p=malloc(n); calls++; if(p)live++; return p; }
static void tracked_free(void *p) { assert(live); live--; free(p); }
static void *failed_alloc(size_t n) { (void)n; calls++; return NULL; }
int main(void) {
    unsigned now=0;
    for(unsigned i=0;i<60;i++) now+=mooncat_animation_delay(now,60);
    assert(now==1000);
    /* A consistently late scheduler must not lose that delay every frame. */
    now=0;
    for(unsigned i=0;i<600;i++) now+=mooncat_animation_delay(now,60)+2;
    assert(now==10002);
    assert(mooncat_animation_delay(137,60)==13); /* skip missed deadlines */
    assert(mooncat_animation_delay(UINT32_MAX,60)>=1);
    assert(mooncat_animation_delay(UINT32_MAX,60)<=17);
    assert(mooncat_animation_delay(0,0)==1);
    for (unsigned a=0; a<256; ++a) for(unsigned b=0; b<256; ++b)
      for(unsigned t=0; t<256; ++t) {
        uint32_t x=a|(b<<8)|((a^85)<<16)|((b^170)<<24);
        uint32_t y=b|(a<<8)|((b^85)<<16)|((a^170)<<24);
        assert(mooncat_blend_argb8888(x,y,t)==reference(x,y,t));
      }
    const char *uniform[]={"a","b","c","d","e","f"};
    struct mooncat_animation_sample_s sample;
    for(unsigned ms=0; ms<2400; ms++) {
        unsigned phase=(ms%1200)*6*256/1200;
        assert(mooncat_animation_sample(uniform,6,1200,ms,&sample));
        assert(sample.current==phase/256 && sample.next==(phase/256+1)%6);
        assert(sample.blend==(phase&255));
    }
    const char *repeated[]={"a","a","a","b","c","c"};
    for(unsigned ms=0; ms<1200; ms++) {
        unsigned t=ms%600, first=t<300?0:t<400?3:4, end=t<300?3:t<400?4:6;
        assert(mooncat_animation_sample(repeated,6,600,ms,&sample));
        assert(sample.current==first && sample.next==end%6);
        assert(sample.blend==((t*6*256/600)-first*256)/(end-first));
    }
    const char *still[]={"a","a","a"};
    assert(mooncat_animation_sample(still,3,300,150,&sample));
    assert(sample.current==0 && sample.next==0);
    assert(!mooncat_animation_sample(uniform,0,100,0,&sample));
    assert(!mooncat_animation_sample(uniform,6,0,0,&sample));
    struct mooncat_frame_cache_s cache={0};
    unsigned char pixels[256], result[256];
    for(unsigned i=0;i<256;i++) pixels[i]=(unsigned char)i;
    mooncat_frame_cache_put(&cache,"frame",pixels,256,tracked_alloc);
    mooncat_frame_cache_put(&cache,"frame",pixels,256,tracked_alloc);
    assert(cache.count==1 && cache.bytes==256 && live==1);
    memset(result,0,256);
    assert(mooncat_frame_cache_get(&cache,"frame",result,256));
    assert(!memcmp(pixels,result,256));
    assert(!mooncat_frame_cache_get(&cache,"frame",result,255));
    pixels[0]=99;
    assert(mooncat_frame_cache_get(&cache,"frame",result,256) && result[0]==0);
    for(unsigned i=0;i<100;i++) {
        char name[32]; snprintf(name,sizeof(name),"frame%u",i);
        mooncat_frame_cache_put(&cache,name,pixels,256,tracked_alloc);
    }
    assert(cache.count==MOONCAT_FRAME_CACHE_SLOTS && live==MOONCAT_FRAME_CACHE_SLOTS);
    mooncat_frame_cache_clear(&cache,tracked_free);
    assert(!live && !cache.bytes && !cache.count);
    void *large=calloc(1,MOONCAT_FRAME_CACHE_BUDGET);
    assert(large);
    mooncat_frame_cache_put(&cache,"large",large,MOONCAT_FRAME_CACHE_BUDGET,tracked_alloc);
    mooncat_frame_cache_put(&cache,"overflow",pixels,256,tracked_alloc);
    assert(cache.count==1 && cache.bytes==MOONCAT_FRAME_CACHE_BUDGET);
    mooncat_frame_cache_clear(&cache,tracked_free); free(large);
    unsigned before=calls;
    mooncat_frame_cache_put(&cache,"fail",pixels,256,failed_alloc);
    mooncat_frame_cache_put(&cache,"fail-again",pixels,256,failed_alloc);
    assert(cache.allocation_failed && calls==before+1 && cache.count==0);
    mooncat_frame_cache_clear(&cache,tracked_free);
    mooncat_frame_cache_put(&cache,"recovered",pixels,256,tracked_alloc);
    assert(live==1); mooncat_frame_cache_clear(&cache,tracked_free); assert(!live);
    puts("16,777,216 packed pixel cases; timing and cache checks passed");
    return 0;
}
''')
    executable = tmp_path/'animation_test'
    subprocess.run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I', str(ROOT/'firmware/openvela_ui'), str(source),
                    '-o', str(executable)], check=True)
    run = subprocess.run([str(executable)], capture_output=True, text=True, check=True)
    assert '16,777,216' in run.stdout
