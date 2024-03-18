#include "esp_err.h"

#include "periph_service.h"
#include "input_key_service.h"
#include "board.h"

#include "lvgl.h"
#include "ui_common.h"

// Local handles for all of the UI elements
static lv_obj_t * s_title_bar = NULL;
static lv_obj_t * s_top_bar = NULL;

void ui_np_set_song_title(const char *title) {
    if (s_title_bar == NULL)
        return;

    lv_label_set_text(s_title_bar, title);
}

lv_obj_t *ui_np_init(void) {
    lv_disp_t *disp = ui_get_display();
    if (disp == NULL) {
        return NULL;
    }
    lv_obj_t *scr = lv_obj_create(NULL);

    // Create a status bar
    s_top_bar = ui_create_top_bar(scr);

    // Create a song-title section
    s_title_bar = lv_label_create(scr);
    lv_label_set_text(s_title_bar, "Play a song! Like maybe... ring ring ring ring ring ring ring banana phoooooone!");
    lv_label_set_long_mode(s_title_bar, LV_LABEL_LONG_SCROLL_CIRCULAR); /* Circular scroll */
    lv_obj_set_width(s_title_bar, disp->driver->hor_res);
    lv_obj_align(s_title_bar, LV_ALIGN_TOP_MID, 0, 12);

    return scr;
}

esp_err_t ui_np_handle_input(periph_service_handle_t handle, periph_service_event_t *evt, audio_board_handle_t board_handle) {
//    int player_volume;
//    audio_hal_get_volume(board_handle->audio_hal, &player_volume);
//
//    if (evt->type == INPUT_KEY_SERVICE_ACTION_CLICK_RELEASE) {
//        switch ((int)evt->data) {
//            case INPUT_KEY_USER_ID_CENTER:
//                ESP_LOGI(TAG, "[ * ] [Play] input key event");
//                audio_element_state_t el_state = audio_element_get_state(i2s_stream_writer);
//                switch (el_state) {
//                    case AEL_STATE_INIT :
//                        ESP_LOGI(TAG, "[ * ] Starting audio pipeline");
//                        ui_set_play(true);
//                        audio_pipeline_run(pipeline);
//                        break;
//                    case AEL_STATE_RUNNING :
//                        ESP_LOGI(TAG, "[ * ] Pausing audio pipeline");
//                        ui_set_play(false);
//                        audio_pipeline_pause(pipeline);
//                        break;
//                    case AEL_STATE_PAUSED :
//                        ESP_LOGI(TAG, "[ * ] Resuming audio pipeline");
//                        ui_set_play(true);
//                        audio_pipeline_resume(pipeline);
//                        break;
//                    default :
//                        ESP_LOGI(TAG, "[ * ] Not supported state %d", el_state);
//                }
//                break;
//            case INPUT_KEY_USER_ID_LEFT:
//                ESP_LOGI(TAG, "[ * ] [Set] input key event");
//                ESP_LOGI(TAG, "[ * ] Stopped, advancing to the next song");
//                char *url = NULL;
//                audio_pipeline_stop(pipeline);
//                audio_pipeline_wait_for_stop(pipeline);
//                audio_pipeline_terminate(pipeline);
//                sdcard_list_next(sdcard_list_handle, 1, &url);
//                ui_np_set_song_title(url);
//                ESP_LOGW(TAG, "URL: %s", url);
//                audio_element_set_uri(fatfs_stream_reader, url);
//                audio_pipeline_reset_ringbuffer(pipeline);
//                audio_pipeline_reset_elements(pipeline);
//                audio_pipeline_run(pipeline);
//                break;
//            case INPUT_KEY_USER_ID_UP:
//                ESP_LOGI(TAG, "[ * ] [Vol+] input key event");
//                player_volume += 2;
//                if (player_volume > 100) {
//                    player_volume = 100;
//                }
//                audio_hal_set_volume(board_handle->audio_hal, player_volume);
//                ESP_LOGI(TAG, "[ * ] Volume set to %d %%", player_volume);
//                break;
//            case INPUT_KEY_USER_ID_DOWN:
//                ESP_LOGI(TAG, "[ * ] [Vol-] input key event");
//                player_volume -= 2;
//                if (player_volume < 0) {
//                    player_volume = 0;
//                }
//                audio_hal_set_volume(board_handle->audio_hal, player_volume);
//                ESP_LOGI(TAG, "[ * ] Volume set to %d %%", player_volume);
//                break;
//        }
//        if (player_volume > 66) {
//            ui_set_volume(2);
//        } else if (player_volume > 28) {
//            ui_set_volume(1);
//        } else {
//            ui_set_volume(0);
//        }
//    }

    return ESP_OK;
}

