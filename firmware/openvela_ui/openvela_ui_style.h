/* Moonlit palette shared by reminder surfaces and notifications. */
#pragma once
#include <lvgl/lvgl.h>
#define MOONCAT_INK 0xf1e8d8
#define MOONCAT_MUTED 0xb9adc9
#define MOONCAT_PANEL 0x14172f
#define MOONCAT_PURPLE 0x49355f
#define MOONCAT_EDGE 0x79658d
static inline void mooncat_surface(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj,lv_color_hex(MOONCAT_PANEL),0);
    lv_obj_set_style_bg_opa(obj,245,0);
    lv_obj_set_style_border_color(obj,lv_color_hex(MOONCAT_EDGE),0);
    lv_obj_set_style_border_width(obj,1,0);
    lv_obj_set_style_radius(obj,18,0);
    lv_obj_set_style_text_color(obj,lv_color_hex(MOONCAT_INK),0);
    lv_obj_set_style_pad_all(obj,10,0);
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
}
static inline void mooncat_button_style(lv_obj_t *obj)
{
    lv_obj_set_style_bg_color(obj,lv_color_hex(MOONCAT_PURPLE),0);
    lv_obj_set_style_bg_opa(obj,LV_OPA_COVER,0);
    lv_obj_set_style_border_color(obj,lv_color_hex(MOONCAT_EDGE),0);
    lv_obj_set_style_border_width(obj,1,0);
    lv_obj_set_style_radius(obj,10,0);
    lv_obj_set_style_text_color(obj,lv_color_hex(MOONCAT_INK),0);
    lv_obj_set_style_pad_all(obj,2,0);
    lv_obj_set_style_shadow_width(obj,0,0);
}
