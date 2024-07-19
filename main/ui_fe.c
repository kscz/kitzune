#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>

#include "esp_err.h"
#include "esp_log.h"

#include "periph_service.h"
#include "input_key_service.h"
#include "board.h"

#include "lvgl.h"
#include "ui_common.h"
#include "ui_fe.h"

#define TAG "UI_FE"

typedef struct {
    lv_obj_t * list_handle;
} ui_fe_item_t;

// Local handles for all of the UI elements
static lv_obj_t * s_screen = NULL;
static lv_obj_t * s_top_bar = NULL;
static lv_obj_t * s_fe_menu = NULL;
static ui_fe_item_t *s_fe_list = NULL;
static size_t s_fe_list_size = 0;
static size_t s_fe_list_count = 0;

static size_t s_hl_line = 0;

static void set_highlighted_line(size_t line) {
    // Clear the old highlight
    lv_obj_set_style_text_color(s_fe_list[s_hl_line].list_handle, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_fe_list[s_hl_line].list_handle, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_long_mode(s_fe_list[s_hl_line].list_handle, LV_LABEL_LONG_CLIP);

    s_hl_line = line;

    // Set the new highlight
    lv_obj_set_style_text_color(s_fe_list[s_hl_line].list_handle, lv_color_white(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_fe_list[s_hl_line].list_handle, lv_color_black(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_fe_list[s_hl_line].list_handle, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_label_set_long_mode(s_fe_list[s_hl_line].list_handle, LV_LABEL_LONG_SCROLL_CIRCULAR);
}

void add_dir_ent(const struct dirent *ent) {
    if (s_fe_list_size == s_fe_list_count) {
        size_t new_size = s_fe_list_size * 2;
        new_size = new_size == 0 ? 4 : new_size;
        ui_fe_item_t *new_fe_list = realloc(s_fe_list, sizeof(ui_fe_item_t) * new_size);
        if (new_fe_list == NULL) {
            perror("Unable to allocate larger list for file explorer!");
            while(1) {}
        }
        s_fe_list_size = new_size;
        s_fe_list = new_fe_list;
    }

    size_t cur = s_fe_list_count;
    s_fe_list_count++;
    s_fe_list[cur].list_handle = lv_list_add_text(s_fe_menu, ent->d_name);
    lv_label_set_long_mode(s_fe_list[cur].list_handle, LV_LABEL_LONG_CLIP);
}

void create_dir_list(const char *dir) {
    DIR *dp;
    struct dirent *ep;
    dp = opendir(dir);
    if (dp != NULL) {
        while ((ep = readdir (dp)) != NULL) {
            printf("%s\n", ep->d_name);
            add_dir_ent(ep);
        }

        closedir(dp);
    } else {
        perror ("Couldn't open the directory");
    }
}

esp_err_t ui_fe_init(void) {
    lv_disp_t *disp = ui_get_display();
    if (disp == NULL) {
        return ESP_FAIL;
    }
    s_screen = lv_obj_create(NULL);

    // Create a status bar
    s_top_bar = ui_create_top_bar(s_screen);

    // Create a song-title section
    s_fe_menu = lv_list_create(s_screen);
    lv_obj_set_width(s_fe_menu, disp->driver->hor_res);
    lv_obj_align(s_fe_menu, LV_ALIGN_TOP_MID, 0, 12);
    create_dir_list("/sdcard");

    return ESP_OK;
}

lv_obj_t *ui_fe_get_screen(void) {
    return s_screen;
}

disp_state_t ui_fe_handle_input(periph_service_handle_t handle, periph_service_event_t *evt, audio_board_handle_t board_handle) {
    if (evt->type == INPUT_KEY_SERVICE_ACTION_CLICK_RELEASE) {
        switch ((int)evt->data) {
            case INPUT_KEY_USER_ID_UP: {
                if (s_hl_line > 0) {
                    set_highlighted_line(s_hl_line - 1);
                    lv_coord_t y = lv_obj_get_height(s_fe_list[s_hl_line].list_handle);
                    lv_obj_scroll_by(s_fe_menu, 0, y, LV_ANIM_ON);
                    //lv_coord_t y = lv_obj_get_y(s_fe_list[s_hl_line].list_handle);
                    //lv_obj_scroll_to_y(s_fe_menu, y, LV_ANIM_ON);
                }
                break;
            }
            case INPUT_KEY_USER_ID_DOWN: {
                if (s_hl_line < s_fe_list_count - 1) {
                    set_highlighted_line(s_hl_line + 1);
                    lv_coord_t y = lv_obj_get_height(s_fe_list[s_hl_line].list_handle);
                    lv_obj_scroll_by(s_fe_menu, 0, -y, LV_ANIM_ON);
                    //lv_coord_t y = lv_obj_get_y(s_fe_list[s_hl_line].list_handle);
                    //lv_obj_scroll_to_y(s_fe_menu, y, LV_ANIM_ON);
                    break;
                }
            }
            case INPUT_KEY_USER_ID_CENTER:
            case INPUT_KEY_USER_ID_LEFT:
            case INPUT_KEY_USER_ID_RIGHT:
            default:
                break;
        }
    }

    return DS_NO_CHANGE;
}
