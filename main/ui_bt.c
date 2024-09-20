#include <string.h>

#include "esp_err.h"
#include "esp_log.h"

#include "periph_service.h"
#include "input_key_service.h"
#include "board.h"

#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "ui_common.h"
#include "bt_be.h"
#include "ui_bt.h"

#include "playlist.h"
#include "player_be.h"

#define TAG "UI_BT"

typedef enum {
    UIBT_INIT,
    UIBT_DISCOVERING,
    UIBT_SELECTING,
} ui_bt_state_t;

typedef struct {
    lv_obj_t * list_handle;
    esp_bd_addr_t bda;
} ui_bt_item_t;

// Local handles for all of the UI elements
static lv_obj_t * s_screen = NULL;
static lv_obj_t * s_top_bar = NULL;
static lv_obj_t * s_bt_menu = NULL;
static ui_bt_item_t s_bt_list[16];
static size_t s_bt_list_count = 0;

static ui_bt_state_t s_state = UIBT_INIT;
static size_t s_hl_line = 0;

static void set_highlighted_line(size_t line) {
    lvgl_port_lock(0);
    // Clear the old highlight
    lv_obj_set_style_text_color(s_bt_list[s_hl_line].list_handle, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_bt_list[s_hl_line].list_handle, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);

    s_hl_line = line;

    // Set the new highlight
    lv_obj_set_style_text_color(s_bt_list[s_hl_line].list_handle, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_bt_list[s_hl_line].list_handle, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_bt_list[s_hl_line].list_handle, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lvgl_port_unlock();
}

esp_err_t ui_bt_init(void) {
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
    s_bt_menu = lv_list_create(s_screen);
    s_bt_list_count = 0;
    if (esp_bt_gap_get_bond_device_num() == 1) {
        s_bt_list[s_bt_list_count].list_handle = lv_list_add_text(s_bt_menu, "Re-connect");
        s_bt_list_count++;
    }
    s_bt_list[s_bt_list_count].list_handle = lv_list_add_text(s_bt_menu, "Start discovery");
    lv_obj_set_width(s_bt_menu, LV_HOR_RES);
    s_bt_list_count++;
    set_highlighted_line(0);
    lv_obj_align(s_bt_menu, LV_ALIGN_TOP_MID, 0, 12);
    lvgl_port_unlock();

    return ESP_OK;
}

static void ui_bt_discovery_complete(bt_dev_info_t *dev, size_t dev_count) {
    lv_disp_t *disp = ui_get_display();

    lvgl_port_lock(0);
    lv_obj_del(s_bt_menu);
    s_bt_menu = lv_list_create(s_screen);
    lv_obj_set_width(s_bt_menu, LV_HOR_RES);
    lv_obj_align(s_bt_menu, LV_ALIGN_TOP_MID, 0, 12);

    s_bt_list_count = 0;
    s_bt_list[s_bt_list_count].list_handle = lv_list_add_text(s_bt_menu, "Re-start discovery");
    s_bt_list_count += 1;
    for (size_t i = 0; i < dev_count; ++i) {
        s_bt_list[s_bt_list_count].list_handle = lv_list_add_text(s_bt_menu, (char *)dev[i].bdname);
        memcpy(s_bt_list[s_bt_list_count].bda, dev[i].bda, sizeof(s_bt_list[s_bt_list_count].bda));
        s_bt_list_count += 1;
    }
    lvgl_port_unlock();

    set_highlighted_line(0);
    s_state = UIBT_SELECTING;
}

lv_obj_t *ui_bt_get_screen(void) {
    return s_screen;
}

lv_obj_t *ui_bt_get_top_bar(void) {
    return s_top_bar;
}

disp_state_t ui_bt_handle_input(periph_service_handle_t handle, periph_service_event_t *evt, audio_board_handle_t board_handle) {
    if (evt->type == INPUT_KEY_SERVICE_ACTION_CLICK_RELEASE) {
        switch ((int)evt->data) {
            case INPUT_KEY_USER_ID_CENTER:
                if (s_state == UIBT_INIT) {
                    if (s_hl_line == 0 && s_bt_list_count > 1) {
                        if (esp_bt_gap_get_bond_device_num() == 1) {
                            int devices_size = 1;
                            esp_bd_addr_t device;
                            if (ESP_OK == esp_bt_gap_get_bond_device_list(&devices_size, device)) {
                                // Connect BT backend to selected device
                                bt_be_connect_ad2p(device);
                                player_be_set_bt_hp();
                            }
                        }
                    } else {
                        if (bt_be_start_discovery(ui_bt_discovery_complete) == ESP_OK) {
                            lvgl_port_lock(0);
                            lv_obj_del(s_bt_menu);
                            s_bt_menu = lv_label_create(s_screen);
                            lv_label_set_text(s_bt_menu, "Discovering...");
                            lv_obj_set_width(s_bt_menu, LV_HOR_RES);
                            lv_obj_align(s_bt_menu, LV_ALIGN_TOP_MID, 0, 12);
                            lvgl_port_unlock();
                            s_state = UIBT_DISCOVERING;
                        }
                    }
                } else if (s_state == UIBT_SELECTING) {
                    if (s_hl_line == 0) {
                        if (bt_be_start_discovery(ui_bt_discovery_complete) == ESP_OK) {
                            lvgl_port_lock(0);
                            lv_obj_del(s_bt_menu);
                            s_bt_menu = lv_label_create(s_screen);
                            lv_label_set_text(s_bt_menu, "Discovering...");
                            lv_obj_set_width(s_bt_menu, LV_HOR_RES);
                            lv_obj_align(s_bt_menu, LV_ALIGN_TOP_MID, 0, 12);
                            lvgl_port_unlock();
                            s_state = UIBT_DISCOVERING;
                        }
                    } else {
                        // Connect BT backend to selected device
                        bt_be_connect_ad2p(s_bt_list[s_hl_line].bda);
                        player_be_set_bt_hp();
                    }
                }
                break;
            case INPUT_KEY_USER_ID_UP:
                if (s_hl_line != 0) {
                    set_highlighted_line(s_hl_line - 1);
                }
                break;
            case INPUT_KEY_USER_ID_DOWN:
                if (s_hl_line != s_bt_list_count - 1) {
                    set_highlighted_line(s_hl_line + 1);
                }
                break;
            case INPUT_KEY_USER_ID_LEFT:
            case INPUT_KEY_USER_ID_RIGHT:
            default:
                break;
        }
    }

    return DS_NO_CHANGE;
}
