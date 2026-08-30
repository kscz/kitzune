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

// Discovery can return 16 devices, plus the re-scan row
#define UIBT_MAX_ITEMS 17

typedef enum {
    UIBT_MENU,
    UIBT_DISCOVERING,
} ui_bt_state_t;

typedef enum {
    UIBT_ACT_DISCOVER,
    UIBT_ACT_CONNECT,
    UIBT_ACT_FORGET,
} ui_bt_action_t;

typedef struct {
    lv_obj_t * list_handle;
    esp_bd_addr_t bda;
    char name[BT_BE_NAME_LEN];
    ui_bt_action_t action;
} ui_bt_item_t;

// Local handles for all of the UI elements
static lv_obj_t * s_screen = NULL;
static lv_obj_t * s_top_bar = NULL;
static lv_obj_t * s_bt_menu = NULL;
static ui_bt_item_t s_bt_list[UIBT_MAX_ITEMS];
static size_t s_bt_list_count = 0;

static ui_bt_state_t s_state = UIBT_MENU;
static size_t s_hl_line = 0;

static void ui_bt_discovery_complete(bt_dev_info_t *dev, size_t dev_count);

// Caller must hold the LVGL lock.
static void set_highlighted_line(size_t line) {
    // Clear the old highlight
    lv_obj_set_style_text_color(s_bt_list[s_hl_line].list_handle, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_bt_list[s_hl_line].list_handle, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);

    s_hl_line = line;

    // Set the new highlight
    lv_obj_set_style_text_color(s_bt_list[s_hl_line].list_handle, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_bt_list[s_hl_line].list_handle, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_bt_list[s_hl_line].list_handle, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

// Scroll the line into the viewport if it isn't already.
// Caller must hold the LVGL lock.
static void scroll_line_to_view(size_t line) {
    lv_coord_t line_y = lv_obj_get_y(s_bt_list[line].list_handle);
    lv_coord_t line_h = lv_obj_get_height(s_bt_list[line].list_handle);

    lv_coord_t scroll_y = lv_obj_get_scroll_y(s_bt_menu);
    lv_coord_t scroll_h = lv_obj_get_height(s_bt_menu);

    if (line_y + line_h > scroll_y + scroll_h) {
        lv_obj_scroll_to_y(s_bt_menu, line_y + line_h - scroll_h, LV_ANIM_ON);
    } else if (line_y < scroll_y) {
        lv_obj_scroll_to_y(s_bt_menu, line_y, LV_ANIM_ON);
    }
}

static void select_line(size_t line) {
    lvgl_port_lock(0);
    set_highlighted_line(line);
    scroll_line_to_view(line);
    lvgl_port_unlock();
}

// Drop every row. The highlight has to go first: its handle dies with the row.
// Caller must hold the LVGL lock.
static void clear_menu(void) {
    lv_obj_clean(s_bt_menu);
    s_bt_list_count = 0;
    s_hl_line = 0;
}

// Caller must hold the LVGL lock.
static ui_bt_item_t *add_item(const char *label) {
    if (s_bt_list_count >= UIBT_MAX_ITEMS) {
        ESP_LOGW(TAG, "No room for menu entry %s", label);
        return NULL;
    }

    ui_bt_item_t *item = &s_bt_list[s_bt_list_count];
    memset(item, 0, sizeof(*item));
    item->list_handle = lv_list_add_text(s_bt_menu, label);
    lv_label_set_long_mode(item->list_handle, LV_LABEL_LONG_CLIP);
    s_bt_list_count += 1;

    return item;
}

// Caller must hold the LVGL lock.
static void add_device_item(const char *name, const esp_bd_addr_t bda) {
    bool named = (name != NULL && name[0] != '\0');
    ui_bt_item_t *item = add_item(named ? name : "(unnamed)");
    if (item == NULL) {
        return;
    }

    memcpy(item->bda, bda, sizeof(item->bda));
    if (named) {
        snprintf(item->name, sizeof(item->name), "%s", name);
    }
    item->action = UIBT_ACT_CONNECT;
}

// Bonded devices first, since re-connecting is the common case
static void show_paired_menu(void) {
    bt_paired_dev_t paired[BT_BE_MAX_PAIRED];
    size_t paired_count = bt_be_get_paired_devices(paired, BT_BE_MAX_PAIRED);

    lvgl_port_lock(0);
    clear_menu();
    for (size_t i = 0; i < paired_count; ++i) {
        add_device_item(paired[i].name, paired[i].bda);
    }
    add_item("Start discovery");

    // Last, so it isn't sat next to the devices it wipes
    if (paired_count != 0) {
        ui_bt_item_t *forget = add_item("Clear pairings");
        if (forget != NULL) {
            forget->action = UIBT_ACT_FORGET;
        }
    }

    set_highlighted_line(0);
    lv_obj_scroll_to_y(s_bt_menu, 0, LV_ANIM_OFF);
    lvgl_port_unlock();

    s_state = UIBT_MENU;
}

static void start_discovery(void) {
    if (bt_be_start_discovery(ui_bt_discovery_complete) != ESP_OK) {
        ESP_LOGW(TAG, "Unable to start discovery");
        return;
    }

    lvgl_port_lock(0);
    clear_menu();
    add_item("Discovering...");
    set_highlighted_line(0);
    lvgl_port_unlock();

    s_state = UIBT_DISCOVERING;
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

    // Create the device menu
    lvgl_port_lock(0);
    s_bt_menu = lv_list_create(s_screen);
    lv_obj_set_width(s_bt_menu, LV_HOR_RES);
    lv_obj_set_height(s_bt_menu, LV_VER_RES - 12);
    lv_obj_align(s_bt_menu, LV_ALIGN_TOP_MID, 0, 12);
    lvgl_port_unlock();

    show_paired_menu();

    return ESP_OK;
}

// Bonds may have changed while we were away, so rebuild on entry
void ui_bt_enter(void) {
    if (s_state != UIBT_DISCOVERING) {
        show_paired_menu();
    }
}

static void ui_bt_discovery_complete(bt_dev_info_t *dev, size_t dev_count) {
    lvgl_port_lock(0);
    clear_menu();
    add_item("Re-start discovery");
    for (size_t i = 0; i < dev_count; ++i) {
        add_device_item((const char *)dev[i].bdname, dev[i].bda);
    }
    set_highlighted_line(0);
    lv_obj_scroll_to_y(s_bt_menu, 0, LV_ANIM_OFF);
    lvgl_port_unlock();

    s_state = UIBT_MENU;
}

lv_obj_t *ui_bt_get_screen(void) {
    return s_screen;
}

disp_state_t ui_bt_handle_input(periph_service_handle_t handle, periph_service_event_t *evt, audio_board_handle_t board_handle) {
    if (evt->type == INPUT_KEY_SERVICE_ACTION_CLICK_RELEASE) {
        switch ((int)evt->data) {
            case INPUT_KEY_USER_ID_CENTER:
                if (!bt_be_is_enabled()) {
                    ESP_LOGW(TAG, "Bluetooth is disabled");
                } else if (s_state == UIBT_DISCOVERING) {
                    ESP_LOGI(TAG, "Discovery already running");
                } else if (s_bt_list[s_hl_line].action == UIBT_ACT_CONNECT) {
                    if (!player_be_bt_ready()) {
                        ESP_LOGW(TAG, "Audio output isn't ready yet");
                        break;
                    }
                    // Connect BT backend to selected device
                    bt_be_connect_ad2p(s_bt_list[s_hl_line].bda, s_bt_list[s_hl_line].name);
                    player_be_set_bt_hp();
                } else if (s_bt_list[s_hl_line].action == UIBT_ACT_FORGET) {
                    if (bt_be_forget_devices() != ESP_OK) {
                        ESP_LOGW(TAG, "Unable to clear every pairing");
                    }
                    show_paired_menu();
                } else {
                    start_discovery();
                }
                break;
            case INPUT_KEY_USER_ID_UP:
                if (s_hl_line != 0) {
                    select_line(s_hl_line - 1);
                }
                break;
            case INPUT_KEY_USER_ID_DOWN:
                if (s_hl_line != s_bt_list_count - 1) {
                    select_line(s_hl_line + 1);
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
