#include "esp_err.h"

#include "periph_service.h"
#include "input_key_service.h"
#include "board.h"

#include "lvgl.h"
#include "ui_common.h"

// Local handles for all of the UI elements
static lv_obj_t * s_screen = NULL;
static lv_obj_t * s_top_bar = NULL;

esp_err_t ui_bt_init(void) {
    lv_disp_t *disp = ui_get_display();
    if (disp == NULL) {
        return ESP_FAIL;
    }
    s_screen = lv_obj_create(NULL);

    // Create a status bar
    s_top_bar = ui_create_top_bar(s_screen);

    // Create a song-title section
    s_title_bar = lv_label_create(s_screen);
    lv_label_set_text(s_title_bar, "Play a song! Like maybe... ring ring ring ring ring ring ring banana phoooooone!");
    lv_label_set_long_mode(s_title_bar, LV_LABEL_LONG_SCROLL_CIRCULAR); /* Circular scroll */
    lv_obj_set_width(s_title_bar, disp->driver->hor_res);
    lv_obj_align(s_title_bar, LV_ALIGN_TOP_MID, 0, 12);

    return ESP_OK;
}

esp_err_t ui_bt_handle_input(periph_service_handle_t handle, periph_service_event_t *evt, audio_board_handle_t board_handle) {
    return ESP_OK;
}

