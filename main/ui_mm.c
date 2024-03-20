#include "esp_err.h"

#include "periph_service.h"
#include "input_key_service.h"
#include "board.h"

#include "lvgl.h"
#include "ui_common.h"

// Local handles for all of the UI elements
static lv_obj_t * s_top_bar = NULL;
static lv_obj_t * s_menu_bar = NULL;
static lv_obj_t * s_mm_screen = NULL;

static uint8_t s_cur_pos = 0;

static const uint16_t s_sel_pos[8][2] = {
    {0, 1},
    {1, 4},
    {4, 7},
    {8, 9},
    {10, 11},
    {11, 14},
    {15, 18},
    {19, 20}
};

lv_obj_t *ui_mm_get_screen(void) {
    return s_mm_screen;
}

static void ui_mm_sel_pos(uint8_t pos) {
    lv_label_set_text_sel_start(s_menu_bar, LV_LABEL_TEXT_SELECTION_OFF);
    lv_label_set_text_sel_end(s_menu_bar, LV_LABEL_TEXT_SELECTION_OFF);

    if (pos < 8) {
        lv_label_set_text_sel_start(s_menu_bar, s_sel_pos[pos][0]);
        lv_label_set_text_sel_end(s_menu_bar, s_sel_pos[pos][1]);
    }
}

esp_err_t ui_mm_init(void) {
    lv_disp_t *disp = ui_get_display();
    if (disp == NULL) {
        return ESP_FAIL;
    }
    s_mm_screen = lv_disp_get_scr_act(disp);

    // Create a status bar
    s_top_bar = ui_create_top_bar(s_mm_screen);

    s_menu_bar = lv_label_create(s_mm_screen);
    lv_label_set_text(s_menu_bar, LV_SYMBOL_HOME " " LV_SYMBOL_LIST "  " LV_SYMBOL_DIRECTORY "  " LV_SYMBOL_CALL "\n" LV_SYMBOL_EDIT " " LV_SYMBOL_DOWNLOAD "   " LV_SYMBOL_BLUETOOTH "  " LV_SYMBOL_SETTINGS);
    ui_add_style_big(s_menu_bar);
    lv_obj_set_width(s_menu_bar, disp->driver->hor_res);
    lv_obj_set_style_text_color(s_menu_bar, lv_color_white(), LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_menu_bar, lv_color_black(), LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_menu_bar, LV_OPA_COVER, LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(s_menu_bar, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_menu_bar, LV_ALIGN_TOP_MID, 0, 14);

    ui_mm_sel_pos(s_cur_pos);

    return ESP_OK;
}

disp_state_t ui_mm_handle_input(periph_service_handle_t handle, periph_service_event_t *evt, audio_board_handle_t board_handle) {
    if (evt->type == INPUT_KEY_SERVICE_ACTION_CLICK_RELEASE) {
        switch ((int)evt->data) {
            case INPUT_KEY_USER_ID_CENTER:
                if (s_cur_pos == 0) {
                    return DS_NOW_PLAYING;
                }
                break;
            case INPUT_KEY_USER_ID_LEFT:
                if (s_cur_pos != 0 && s_cur_pos != 4) {
                    s_cur_pos -= 1;
                    ui_mm_sel_pos(s_cur_pos);
                }
                break;
            case INPUT_KEY_USER_ID_RIGHT:
                if (s_cur_pos != 3 && s_cur_pos != 7) {
                    s_cur_pos += 1;
                    ui_mm_sel_pos(s_cur_pos);
                }
                break;
            case INPUT_KEY_USER_ID_UP:
                if (s_cur_pos >= 4) {
                    s_cur_pos -= 4;
                    ui_mm_sel_pos(s_cur_pos);
                }
                break;
            case INPUT_KEY_USER_ID_DOWN:
                if (s_cur_pos < 4) {
                    s_cur_pos += 4;
                    ui_mm_sel_pos(s_cur_pos);
                }
                break;
        }
    }

    return DS_NO_CHANGE;
}

