#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>

#include "esp_err.h"
#include "esp_log.h"

#include "periph_service.h"
#include "input_key_service.h"
#include "board.h"

#include "playlist.h"
#include "dram_list.h"

#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "kz_util.h"
#include "player_be.h"
#include "ui_common.h"
#include "ui_fe.h"

#define TAG "UI_FE"

typedef struct {
    lv_obj_t * list_handle;
    char *name;
    bool is_dir;
} ui_fe_item_t;

// Local handles
static lv_obj_t * s_screen = NULL;
static lv_obj_t * s_top_bar = NULL;
static lv_obj_t * s_fe_menu = NULL;
static ui_fe_item_t *s_fe_list = NULL;
static size_t s_fe_list_size = 0;
static size_t s_fe_list_count = 0;

static char **s_cur_path = NULL;
static size_t s_cur_path_len = 0, s_cur_path_size = 0;

static size_t s_hl_line = 0;
static lv_coord_t s_hor_res, s_ver_res;

// De-highlight the currently highlighted line and disable circular scroll
// Then highlight the new line and enable circular scroll
static void set_highlighted_line(size_t line) {
    lvgl_port_lock(0);
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
    lvgl_port_unlock();
}

// Check to see if the current line is within the viewport and scroll it if not
static void scroll_line_to_view(size_t line) {
    lvgl_port_lock(0);
    // Get the target line's position and height
    lv_coord_t line_y = lv_obj_get_y(s_fe_list[line].list_handle);
    lv_coord_t line_h = lv_obj_get_height(s_fe_list[line].list_handle);

    // Get the viewport's scroll position and size
    lv_coord_t scroll_y = lv_obj_get_scroll_y(s_fe_menu);
    lv_coord_t scroll_h = lv_obj_get_height(s_fe_menu);

    // See if the line is wholly in view
    if (line_y + line_h > scroll_y + scroll_h) {
        lv_obj_scroll_to_y(s_fe_menu, line_y + line_h - scroll_h, LV_ANIM_ON);
    } else if(line_y < scroll_y) {
        lv_obj_scroll_to_y(s_fe_menu, line_y, LV_ANIM_ON);
    }
    lvgl_port_unlock();
}

// Add the entry to the directory list, growing the allocation if needed
static void add_dir_ent(const char *name, bool is_dir) {
    // Check if we need more space
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

    // Add in the new entry to the directory listing
    size_t cur = s_fe_list_count;
    s_fe_list_count++;
    char show_name[264];
    snprintf(show_name, sizeof(show_name), "%s %s", (is_dir ? LV_SYMBOL_DIRECTORY : ""), name);
    lvgl_port_lock(0);
    s_fe_list[cur].list_handle = lv_list_add_text(s_fe_menu, show_name);
    lv_label_set_long_mode(s_fe_list[cur].list_handle, LV_LABEL_LONG_CLIP);
    lvgl_port_unlock();
    s_fe_list[cur].is_dir = is_dir;

    size_t name_len = strlen(name);
    s_fe_list[cur].name = malloc(name_len + 1);
    if (s_fe_list[cur].name == NULL) {
        ESP_LOGE(TAG, "Unable to allocate directory entry space!");
        while(1) {}
    }
    strcpy(s_fe_list[cur].name, name);
}

// Iterate through the directory and create a list of all the files
static void create_dir_list(const char *dir) {
    DIR *dp;
    struct dirent *ep;
    if (s_cur_path_len != 0) {
        add_dir_ent(LV_SYMBOL_UP " Up Directory", false);
    }

    add_dir_ent(" " LV_SYMBOL_PLAY " Play All", false);

    dp = opendir(dir);
    if (dp != NULL) {
        while ((ep = readdir (dp)) != NULL) {
            add_dir_ent(ep->d_name, (ep->d_type == DT_DIR));
        }

        closedir(dp);
    } else {
        perror ("Couldn't open the directory");
    }
}

// Delete all the children of the current list
static void clear_dir_list() {
    lvgl_port_lock(0);
    lv_obj_clean(s_fe_menu);
    lvgl_port_unlock();
    for (size_t i = 0; i < s_fe_list_count; ++i) {
        free(s_fe_list[i].name);
        s_fe_list[i].name = NULL;
    }
    s_fe_list_count = 0;
    s_hl_line = 0;
}

// Add a path chunk to the current path array
static void push_cur_path(const char *path_segment) {
    if (s_cur_path_len == s_cur_path_size) {
        s_cur_path_size = s_cur_path_size == 0 ? 2 : s_cur_path_size * 2;
        s_cur_path = realloc(s_cur_path, sizeof(*s_cur_path) * s_cur_path_size);
        if (s_cur_path == NULL) {
            ESP_LOGE(TAG, "Failed to allocate space!");
            while(1) {}
        }
    }
    const size_t path_segment_len = strlen(path_segment);
    s_cur_path[s_cur_path_len] = malloc(path_segment_len + 1);
    if (s_cur_path[s_cur_path_len] == NULL) {
        ESP_LOGE(TAG, "Failed to allocate space!");
        while(1) {}
    }
    strcpy(s_cur_path[s_cur_path_len], path_segment);
    s_cur_path_len++;
}

// Remove the last entry of the current path array
static void pop_cur_path(void) {
    s_cur_path_len--;
    free(s_cur_path[s_cur_path_len]);
    s_cur_path[s_cur_path_len] = NULL;
}

// Create the initial screen with the SD card root directory listing
esp_err_t ui_fe_init(void) {
    lv_disp_t *disp = ui_get_display();
    if (disp == NULL) {
        return ESP_FAIL;
    }
    s_hor_res = disp->driver->hor_res;
    s_ver_res = disp->driver->ver_res;
    s_screen = lv_obj_create(NULL);

    // Create a status bar
    s_top_bar = ui_create_top_bar(s_screen);

    // Create a file explorer section
    lvgl_port_lock(0);
    s_fe_menu = lv_list_create(s_screen);
    lv_obj_set_width(s_fe_menu, s_hor_res);
    lv_obj_set_height(s_fe_menu, s_ver_res - 12);
    lv_obj_align(s_fe_menu, LV_ALIGN_TOP_MID, 0, 12);
    lvgl_port_unlock();
    create_dir_list("/sdcard");
    set_highlighted_line(0);

    return ESP_OK;
}

lv_obj_t *ui_fe_get_screen(void) {
    return s_screen;
}

// Create a playlist by recursively calling this function to descend into directories
// The parameters here are a bit henious - there's the playlist we're adding to, and then
// a pointer to a string which represents the current base url, and a pointer to the '\0'
// at the end of that base url. That string must also be a large buffer, because we copy
// past the null terminator and don't check our lengths
//
// FIXME - actually bounds check our copying here
static void r_generate_playlist(playlist_operator_handle_t pl, char *curpath, char *curpath_end) {
    DIR *dp;
    struct dirent *ep;

    // FIXME - we use this to skip past the "file:/" in front of the path, but it's gross
    dp = opendir(curpath + 6);
    if (dp == NULL) {
        perror ("Couldn't open the directory");
        return;
    }

    char **dirs = malloc(sizeof(char *) * 4);
    size_t dirs_count = 0, dirs_size = 4;

    while ((ep = readdir (dp)) != NULL) {
        if (ep->d_type == DT_DIR) {
            if (dirs_count == dirs_size) {
                dirs_size = dirs_size * 2;
                dirs = realloc(dirs, sizeof(*dirs) * dirs_size);
            }
            dirs[dirs_count] = malloc(strlen(ep->d_name) + 1);
            strcpy(dirs[dirs_count], ep->d_name);
            dirs_count++;
        } else {
            char *new_end = curpath_end;
            *new_end = '/';
            new_end++;
            new_end = stpcpy(new_end, ep->d_name);
            if (AUD_EXT_UNKNOWN != kz_get_ext(curpath)) {
                dram_list_save(pl, curpath);
            }
        }
    }

    closedir(dp);

    for (size_t i = 0; i < dirs_count; ++i) {
        char *new_end = curpath_end;
        *new_end = '/';
        new_end++;
        new_end = stpcpy(new_end, dirs[i]);
        r_generate_playlist(pl, curpath, new_end);
        free(dirs[i]);
        dirs[i] = NULL;
    }
    free(dirs);
    dirs = NULL;
}

// Process input from the front keys
disp_state_t ui_fe_handle_input(periph_service_handle_t handle, periph_service_event_t *evt, audio_board_handle_t board_handle) {
    disp_state_t ret = DS_NO_CHANGE;
    if (evt->type == INPUT_KEY_SERVICE_ACTION_CLICK_RELEASE) {
        bool should_update = false;
        switch ((int)evt->data) {
            case INPUT_KEY_USER_ID_UP: {
                if (s_hl_line > 0) {
                    set_highlighted_line(s_hl_line - 1);
                } else {
                    set_highlighted_line(s_fe_list_count - 1);
                }
                scroll_line_to_view(s_hl_line);
                break;
            }
            case INPUT_KEY_USER_ID_DOWN: {
                if (s_hl_line < s_fe_list_count - 1) {
                    set_highlighted_line(s_hl_line + 1);
                } else {
                    set_highlighted_line(0);
                }
                scroll_line_to_view(s_hl_line);
                break;
            }
            case INPUT_KEY_USER_ID_CENTER: {
                if (s_cur_path_len != 0 && s_hl_line == 0) {
                    pop_cur_path();
                    should_update = true;
                } else if(s_fe_list[s_hl_line].is_dir) {
                    push_cur_path(s_fe_list[s_hl_line].name);
                    should_update = true;
                } else if (!s_fe_list[s_hl_line].is_dir) {
                    // We should fall here if they hit the "play all" button
                    playlist_operator_handle_t pl;
                    if (ESP_OK != dram_list_create(&pl)) {
                        ESP_LOGW(TAG, "Error creating playlist!");
                        break;
                    }
                    // FIXME Actually check the path lengths
                    char basepath[2048];
                    char *cur_p = stpcpy(basepath, "file://sdcard");
                    for (size_t i = 0; i < s_cur_path_len; ++i) {
                        *cur_p = '/';
                        cur_p++;
                        cur_p = stpcpy(cur_p, s_cur_path[i]);
                    }

                    if ((s_cur_path_len == 0 && s_hl_line == 0) ||
                        (s_cur_path_len != 0 && s_hl_line == 1))
                    {
                        r_generate_playlist(pl, basepath, cur_p);
                    } else {
                        *cur_p = '/';
                        cur_p++;
                        cur_p = stpcpy(cur_p, s_fe_list[s_hl_line].name);
                        dram_list_save(pl, basepath);
                    }

                    playlist_operation_t pl_op;
                    pl->get_operation(&pl_op);

                    if (pl_op.get_url_num(pl) != 0) {
                        player_set_playlist(pl, portMAX_DELAY);
                        ret = DS_NOW_PLAYING;
                    } else {
                        pl_op.destroy(pl);
                    }
                }
                break;
            }
            case INPUT_KEY_USER_ID_LEFT:
                if (s_cur_path_len != 0) {
                    pop_cur_path();
                    should_update = true;
                }
                break;
            case INPUT_KEY_USER_ID_RIGHT:
                if(s_fe_list[s_hl_line].is_dir) {
                    push_cur_path(s_fe_list[s_hl_line].name);
                    should_update = true;
                }
                break;
            default:
                break;
        }

        if (should_update) {
            clear_dir_list();

            // FIXME Actually check the path lengths
            char fullpath[1024];
            char *cur_p = stpcpy(fullpath, "/sdcard");
            for (int i = 0; i < s_cur_path_len; ++i) {
                *cur_p = '/';
                cur_p++;
                cur_p = stpcpy(cur_p, s_cur_path[i]);
            }
            create_dir_list(fullpath);
            set_highlighted_line(0);
        }
    }

    return ret;
}
