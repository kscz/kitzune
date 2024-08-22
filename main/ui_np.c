#include "lvgl.h"
#include "esp_lvgl_port.h"

#include "esp_err.h"
#include "esp_log.h"

#include "periph_service.h"
#include "input_key_service.h"
#include "board.h"

#include "playlist.h"
#include "player_be.h"
#include "ui_common.h"
 static const char *TAG = "UI_NP";

// Local handles for all of the UI elements
static lv_obj_t * s_screen = NULL;
static lv_obj_t * s_title_bar = NULL;
static lv_obj_t * s_shuffle_bar = NULL;
static lv_obj_t * s_top_bar = NULL;

void ui_np_set_song_title(const char *title) {
    if (s_title_bar == NULL)
        return;

    lvgl_port_lock(0);
    lv_label_set_text(s_title_bar, title);
    lvgl_port_unlock();
}

lv_obj_t *ui_np_get_screen(void) {
    return s_screen;
}

esp_err_t ui_np_init(void) {
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
    s_title_bar = lv_label_create(s_screen);
    lv_label_set_text(s_title_bar, "Play a song! Like maybe... ring ring ring ring ring ring ring banana phoooooone!");
    lv_label_set_long_mode(s_title_bar, LV_LABEL_LONG_SCROLL_CIRCULAR); /* Circular scroll */
    lv_obj_set_width(s_title_bar, disp->driver->hor_res);
    lv_obj_align(s_title_bar, LV_ALIGN_TOP_MID, 0, 12);
    s_shuffle_bar = lv_label_create(s_screen);
    lv_obj_set_width(s_shuffle_bar, disp->driver->hor_res);
    lv_label_set_text(s_shuffle_bar, LV_SYMBOL_SHUFFLE);
    lv_obj_align(s_shuffle_bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lvgl_port_unlock();

    return ESP_OK;
}

disp_state_t ui_np_handle_input(periph_service_handle_t handle, periph_service_event_t *evt, audio_board_handle_t board_handle) {

    int player_volume;
    audio_hal_get_volume(board_handle->audio_hal, &player_volume);
    if (evt->type == INPUT_KEY_SERVICE_ACTION_CLICK_RELEASE) {
        switch ((int)evt->data) {
            case INPUT_KEY_USER_ID_CENTER:
                player_playpause();
                break;
            case INPUT_KEY_USER_ID_RIGHT:
                player_next();
                break;
            case INPUT_KEY_USER_ID_UP:
                ESP_LOGI(TAG, "[ * ] [Vol+] input key event");
                player_volume += 2;
                if (player_volume > 100) {
                    player_volume = 100;
                }
                audio_hal_set_volume(board_handle->audio_hal, player_volume);
                ESP_LOGI(TAG, "[ * ] Volume set to %d %%", player_volume);
                break;
            case INPUT_KEY_USER_ID_DOWN:
                ESP_LOGI(TAG, "[ * ] [Vol-] input key event");
                player_volume -= 2;
                if (player_volume < 0) {
                    player_volume = 0;
                }
                audio_hal_set_volume(board_handle->audio_hal, player_volume);
                ESP_LOGI(TAG, "[ * ] Volume set to %d %%", player_volume);
                break;
        }
    }

    if (evt->type == INPUT_KEY_SERVICE_ACTION_PRESS) {
        switch ((int)evt->data) {
            case INPUT_KEY_USER_ID_RIGHT:
                player_set_shuffle(!player_get_shuffle());
                lvgl_port_lock(0);
                if (player_get_shuffle()) {
                    lv_label_set_text(s_shuffle_bar, LV_SYMBOL_SHUFFLE);
                } else {
                    lv_label_set_text(s_shuffle_bar, "");
                }
                lvgl_port_unlock();
                break;
            default:
                break;
        }
    }

    return ESP_OK;
}

