#include "esp_err.h"

#include "periph_service.h"
#include "input_key_service.h"
#include "board.h"

#include "lvgl.h"
#include "ui_common.h"

// Local handles for all of the UI elements
static lv_obj_t * s_screen = NULL;
static lv_obj_t * s_top_bar = NULL;
static lv_obj_t * s_bt_menu = NULL;

esp_err_t ui_bt_init(void) {
    lv_disp_t *disp = ui_get_display();
    if (disp == NULL) {
        return ESP_FAIL;
    }
    s_screen = lv_obj_create(NULL);

    // Create a status bar
    s_top_bar = ui_create_top_bar(s_screen);

    // Create a song-title section
    s_bt_menu = lv_label_create(s_screen);
    lv_label_set_text(s_bt_menu, "* Start Discovery!");
    lv_obj_set_width(s_title_bar, disp->driver->hor_res);
    lv_obj_align(s_title_bar, LV_ALIGN_TOP_MID, 0, 12);

    return ESP_OK;
}


disp_state_t ui_bt_handle_input(periph_service_handle_t handle, periph_service_event_t *evt, audio_board_handle_t board_handle) {
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
