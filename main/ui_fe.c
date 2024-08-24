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
#include "dynstr.h"
#include "strstack.h"
#include "kz_util.h"
#include "player_be.h"
#include "ui_common.h"
#include "ui_fe.h"

#define TAG "UI_FE"

#define FILE_PREFIX_LEN 6

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

static strstack_handle_t s_curpath = NULL;

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
    if (strstack_depth(s_curpath) != 0) {
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

// Create the initial screen with the SD card root directory listing
esp_err_t ui_fe_init(void) {
    lv_disp_t *disp = ui_get_display();
    if (disp == NULL) {
        return ESP_FAIL;
    }
    s_curpath = strstack_new();

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

// Create a playlist for a directory
static void generate_directory_playlist(playlist_operator_handle_t pl, dynstr_handle_t curpath) {
    DIR *dp = NULL;
    struct dirent *ep;

    strstack_handle_t dirs = strstack_new();
    if (!strstack_push(dirs, dynstr_as_c_str(curpath))) {
        goto generate_directory_playlist_cleanup;
    }

    while (strstack_depth(dirs) > 0) {
        dynstr_assign(curpath, strstack_peek_top(dirs));
        strstack_pop(dirs);

        dp = opendir(dynstr_as_c_str(curpath) + FILE_PREFIX_LEN);
        if (dp == NULL) {
            perror ("Couldn't open the directory");
            goto generate_directory_playlist_cleanup;
        }

        // Iterate through the current directory and add all the files, saving
        // directories for later
        if (!dynstr_append_c_str(curpath, "/")) {
            goto generate_directory_playlist_cleanup;
        }
        size_t curpath_initial_len = dynstr_len(curpath);
        while ((ep = readdir (dp)) != NULL) {
            dynstr_truncate(curpath, curpath_initial_len);
            if (!dynstr_append_c_str(curpath, ep->d_name)) {
                goto generate_directory_playlist_cleanup;
            }
            const char *complete_path = dynstr_as_c_str(curpath);

            if (ep->d_type == DT_DIR) {
                if (!strstack_push(dirs, complete_path)) {
                    goto generate_directory_playlist_cleanup;
                }
            } else {
                if (AUD_EXT_UNKNOWN != kz_get_ext(complete_path)) {
                    dram_list_save(pl, complete_path);
                }
            }
        }

        // Close our file handle before descending further
        closedir(dp);
        dp = NULL;
    }

generate_directory_playlist_cleanup:
    if (dp != NULL) {
        closedir(dp);
        dp = NULL;
    }
    strstack_destroy(dirs);
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
                if (strstack_depth(s_curpath) != 0 && s_hl_line == 0) {
                    strstack_pop(s_curpath);
                    should_update = true;
                } else if(s_fe_list[s_hl_line].is_dir) {
                    strstack_push(s_curpath, s_fe_list[s_hl_line].name);
                    should_update = true;
                } else if (!s_fe_list[s_hl_line].is_dir) {
                    // We should fall here if they hit the "play all" button or
                    // if they select a file
                    playlist_operator_handle_t pl;
                    if (ESP_OK != dram_list_create(&pl)) {
                        ESP_LOGW(TAG, "Error creating playlist!");
                        break;
                    }

                    // Generate a base URL for the playlist
                    dynstr_handle_t path = dynstr_new();
                    dynstr_append_c_str(path, "file://sdcard");

                    // Iterate through the current path segments and assemble the base URL
                    for (size_t i = 0; i < strstack_depth(s_curpath); ++i) {
                        dynstr_append_c_str(path, "/");
                        dynstr_append_c_str(path, strstack_peek_lifo(s_curpath, i));
                    }

                    // Check if this was the "play all" button or a single file
                    if ((strstack_depth(s_curpath) == 0 && s_hl_line == 0) ||
                        (strstack_depth(s_curpath) != 0 && s_hl_line == 1))
                    {
                        generate_directory_playlist(pl, path);
                    } else {
                        dynstr_append_c_str(path, "/");
                        dynstr_append_c_str(path, s_fe_list[s_hl_line].name);
                        dram_list_save(pl, dynstr_as_c_str(path));
                    }

                    playlist_operation_t pl_op;
                    pl->get_operation(&pl_op);

                    if (pl_op.get_url_num(pl) != 0) {
                        player_set_playlist(pl, portMAX_DELAY);
                        ret = DS_NOW_PLAYING;
                    } else {
                        pl_op.destroy(pl);
                    }

                    dynstr_destroy(path);
                    path = NULL;
                }
                break;
            }
            case INPUT_KEY_USER_ID_LEFT:
                if (strstack_depth(s_curpath) != 0) {
                    strstack_pop(s_curpath);
                    should_update = true;
                }
                break;
            case INPUT_KEY_USER_ID_RIGHT:
                if(s_fe_list[s_hl_line].is_dir) {
                    strstack_push(s_curpath, s_fe_list[s_hl_line].name);
                    should_update = true;
                }
                break;
            default:
                break;
        }

        if (should_update) {
            clear_dir_list();
            dynstr_handle_t path = dynstr_new();

            dynstr_append_c_str(path, "/sdcard");
            for (int i = 0; i < strstack_depth(s_curpath); ++i) {
                dynstr_append_c_str(path, "/");
                dynstr_append_c_str(path, strstack_peek_lifo(s_curpath, i));
            }
            create_dir_list(dynstr_as_c_str(path));
            set_highlighted_line(0);
            dynstr_destroy(path);
        }
    }

    return ret;
}
