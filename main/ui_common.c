#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "ui_common.h"

static bool s_is_playing = false;
static uint8_t s_volume_level = 2;
static uint8_t s_batt_level = 4;

static lv_disp_t * s_disp = NULL;
static lv_style_t s_style_small;
static lv_style_t s_style_big;

void ui_set_play(bool is_playing) {
    s_is_playing = is_playing;
}

void ui_set_volume(uint8_t level) {
    s_volume_level = level;
}

void ui_set_batt_level(uint8_t level) {
    s_batt_level = level;
}

void ui_set_top_bar(lv_obj_t *lbl) {
    if (lbl == NULL)
        return;

    const char * batt_sym = NULL;
    switch(s_batt_level) {
        case 0:
            batt_sym = LV_SYMBOL_BATTERY_EMPTY;
            break;
        case 1:
            batt_sym = LV_SYMBOL_BATTERY_1;
            break;
        case 2:
            batt_sym = LV_SYMBOL_BATTERY_2;
            break;
        case 3:
            batt_sym = LV_SYMBOL_BATTERY_3;
            break;
        default:
            batt_sym = LV_SYMBOL_BATTERY_FULL;
            break;
    }

    const char * vol_sym = NULL;
    switch(s_volume_level) {
        case 0:
            vol_sym = LV_SYMBOL_MUTE "  ";
            break;
        case 1:
            vol_sym = LV_SYMBOL_VOLUME_MID " ";
            break;
        default:
            vol_sym = LV_SYMBOL_VOLUME_MAX;
            break;
    }

    lvgl_port_lock(0);
    lv_label_set_text_fmt(lbl, "Kitzune              %s  %s  %s",
            (s_is_playing ? LV_SYMBOL_PLAY : LV_SYMBOL_PAUSE),
            vol_sym, batt_sym);
    lvgl_port_unlock();
}

lv_obj_t *ui_create_top_bar(lv_obj_t *screen) {
    if (screen == NULL) {
        return NULL;
    }

    lvgl_port_lock(0);
    lv_obj_t *top_bar = lv_label_create(screen);
    lvgl_port_unlock();
    ui_set_top_bar(top_bar);
    lvgl_port_lock(0);
    lv_obj_add_style(top_bar, &s_style_small, 0);
    lv_obj_set_width(top_bar, LV_HOR_RES);
    lv_obj_set_style_text_color(top_bar, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(top_bar, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(top_bar, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_align(top_bar, LV_ALIGN_TOP_MID, 0, 0);
    lvgl_port_unlock();

    return top_bar;
}

lv_disp_t *ui_get_display(void) {
    return s_disp;
}

void ui_add_style_small(lv_obj_t * obj) {
    if (obj == NULL) {
        return;
    }

    lv_obj_add_style(obj, &s_style_small, 0);
}

void ui_add_style_big(lv_obj_t * obj) {
    if (obj == NULL) {
        return;
    }

    lv_obj_add_style(obj, &s_style_big, 0);
}

void ui_common_init(lv_disp_t *disp) {
    s_disp = disp;

    lvgl_port_lock(0);
    lv_style_init(&s_style_small);
    lv_style_set_text_font(&s_style_small, &lv_font_montserrat_10);
    lv_style_init(&s_style_big);
    lv_style_set_text_font(&s_style_big, &lv_font_montserrat_22);
    lvgl_port_unlock();
}
