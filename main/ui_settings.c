#include "lvgl.h"
#include "esp_lvgl_port.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"

#include "periph_service.h"
#include "input_key_service.h"
#include "board.h"

#include "playlist.h"
#include "player_be.h"
#include "ui_common.h"
 static const char *TAG = "UI_SET";

// Local handles for all of the UI elements
static lv_obj_t * s_screen = NULL;
static lv_obj_t * s_menu = NULL;
static lv_obj_t * s_top_bar = NULL;

static lv_obj_t * s_menu_items[2];

static size_t s_hl_line = 0;

// De-highlight the currently highlighted line and disable circular scroll
// Then highlight the new line and enable circular scroll
static void set_highlighted_line(size_t line) {
    lvgl_port_lock(0);
    // Clear the old highlight
    lv_obj_set_style_text_color(s_menu_items[s_hl_line], lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_menu_items[s_hl_line], lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);

    s_hl_line = line;

    // Set the new highlight
    lv_obj_set_style_text_color(s_menu_items[s_hl_line], lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_menu_items[s_hl_line], lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_menu_items[s_hl_line], LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lvgl_port_unlock();
}

lv_obj_t *ui_settings_get_screen(void) {
    return s_screen;
}

esp_err_t ui_settings_init(void) {
    lv_disp_t *disp = ui_get_display();
    if (disp == NULL) {
        return ESP_FAIL;
    }
    lvgl_port_lock(0);
    s_screen = lv_obj_create(NULL);
    lvgl_port_unlock();

    // Create a status bar
    s_top_bar = ui_create_top_bar(s_screen);

    // Create a song-title section
    lvgl_port_lock(0);
    s_menu = lv_list_create(s_screen);
    lv_obj_set_width(s_menu, disp->driver->hor_res);
    lv_obj_set_height(s_menu, disp->driver->ver_res - 12);
    lv_obj_align(s_menu, LV_ALIGN_TOP_MID, 0, 12);
    s_menu_items[0] = lv_list_add_text(s_menu, "Disable " LV_SYMBOL_WIFI);
    s_menu_items[1] = lv_list_add_text(s_menu, "Disable " LV_SYMBOL_BLUETOOTH);
    lvgl_port_unlock();

    set_highlighted_line(0);

    return ESP_OK;
}

disp_state_t ui_settings_handle_input(periph_service_handle_t handle, periph_service_event_t *evt, audio_board_handle_t board_handle)
{
    // Handle short presses
    if (evt->type == INPUT_KEY_SERVICE_ACTION_CLICK_RELEASE) {
        switch ((int)evt->data) {
            case INPUT_KEY_USER_ID_CENTER:
                if (s_hl_line == 0) {
                } else if (s_hl_line == 1) {
                    esp_bluedroid_disable();
                    esp_bluedroid_deinit();
                    esp_bt_controller_disable();
                    esp_bt_controller_deinit();
                }
                break;
            case INPUT_KEY_USER_ID_UP:
                if (s_hl_line != 0) {
                    set_highlighted_line(s_hl_line - 1);
                }
                break;
            case INPUT_KEY_USER_ID_DOWN:
                if (s_hl_line != (sizeof(s_menu_items)/sizeof(*s_menu_items)) - 1) {
                    set_highlighted_line(s_hl_line + 1);
                }
                break;
        }
    }

    // Handle long-presses
    if (evt->type == INPUT_KEY_SERVICE_ACTION_PRESS) {
        switch ((int)evt->data) {
            default:
                break;
        }
    }

    return ESP_OK;
}

