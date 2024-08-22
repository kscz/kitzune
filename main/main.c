/* Control with a touch pad playing MP3 files from SD Card

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"

#include "esp_peripherals.h"
#include "periph_sdcard.h"
#include "periph_button.h"
#include "input_key_service.h"
#include "periph_adc_button.h"
#include "board.h"
#include "a2dp_stream.h"
#include "playlist.h"

#include "driver/i2c.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_panel_vendor.h"

#include "lvgl.h"
#include "bt_be.h"
#include "player_be.h"
#include "ui_common.h"
#include "ui_mm.h"
#include "ui_np.h"
#include "ui_bt.h"
#include "ui_fe.h"

static const char *TAG = "MAIN";

#define SSD1306_H_RES 128
#define SSD1306_V_RES 64

#if configUSE_TRACE_FACILITY != 0
static esp_err_t print_real_time_stats(TickType_t xTicksToWait);
#endif

static disp_state_t s_cur_disp_state = DS_MAIN_MENU;
static disp_state_t s_prev_disp_state = DS_MAIN_MENU;

static esp_err_t input_key_service_cb(periph_service_handle_t handle, periph_service_event_t *evt, void *ctx)
{
    /* Handle touch pad events
           to start, pause, resume, finish current song and adjust volume
        */
    audio_board_handle_t board_handle = (audio_board_handle_t) ctx;

    disp_state_t next_state = DS_NO_CHANGE;
    if (evt->type == INPUT_KEY_SERVICE_ACTION_PRESS) {
        switch ((int)evt->data) {
            case INPUT_KEY_USER_ID_CENTER:
                if (s_cur_disp_state == DS_MAIN_MENU) {
                    next_state = s_prev_disp_state;
                } else {
                    next_state = DS_MAIN_MENU;
                }
                break;
            default:
                break;
        }
    }

    if (next_state == DS_NO_CHANGE) {
        switch(s_cur_disp_state) {
            case DS_MAIN_MENU:
                next_state = ui_mm_handle_input(handle, evt, board_handle);
                break;
            case DS_NOW_PLAYING:
                next_state = ui_np_handle_input(handle, evt, board_handle);
                break;
            case DS_BLUETOOTH:
                next_state = ui_bt_handle_input(handle, evt, board_handle);
                break;
            case DS_FILE_EXP:
                next_state = ui_fe_handle_input(handle, evt, board_handle);
                break;
            default:
                return ESP_FAIL;
        }
    }

    if (next_state != DS_NO_CHANGE && next_state != s_cur_disp_state) {
        s_prev_disp_state = s_cur_disp_state;
        s_cur_disp_state = next_state;

        lvgl_port_lock(0);
        switch(s_cur_disp_state) {
            case DS_NOW_PLAYING:
                lv_scr_load_anim(ui_np_get_screen(), LV_SCR_LOAD_ANIM_MOVE_TOP, 500, 0, false);
                break;
            case DS_BLUETOOTH:
                lv_scr_load_anim(ui_bt_get_screen(), LV_SCR_LOAD_ANIM_MOVE_TOP, 500, 0, false);
                break;
            case DS_FILE_EXP:
                lv_scr_load_anim(ui_fe_get_screen(), LV_SCR_LOAD_ANIM_MOVE_TOP, 500, 0, false);
                break;
            case DS_MAIN_MENU:
            default:
                lv_scr_load_anim(ui_mm_get_screen(), LV_SCR_LOAD_ANIM_MOVE_BOTTOM, 500, 0, false);
                break;
        }
        lvgl_port_unlock();
    }

    return ESP_OK;
}

void app_main(void)
{
    /* Initialize NVS — it is used to store PHY calibration data and save key-value pairs in flash memory*/
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    /* This GPIO controls the SDMMC pullups - pull them up! */
    gpio_reset_pin(26);
    gpio_set_direction(26, GPIO_MODE_OUTPUT);
    gpio_set_level(26, 1);

    /* Force reset the screen! */
    gpio_reset_pin(21);
    gpio_set_direction(21, GPIO_MODE_OUTPUT);
    gpio_set_level(21, 0);

    ESP_LOGI(TAG, "[1.0] Initialize peripherals management");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    esp_periph_set_handle_t set = esp_periph_set_init(&periph_cfg);

    ESP_LOGI(TAG, "[1.1] Initialize and start peripherals");
    audio_board_key_init(set);
    audio_board_sdcard_init(set, SD_MODE_4_LINE);

    bt_be_init();

    // launch player_backend task!
    xTaskCreatePinnedToCore(player_main, "PLAYER", (8*1024), NULL, 1, NULL, APP_CPU_NUM);
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "[ 2 ] Start codec chip");
    audio_board_handle_t board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);

    audio_hal_set_volume(board_handle->audio_hal, 50);

    ESP_LOGI(TAG, "[ 3 ] Create and start input key service");
    input_key_service_info_t input_key_info[] = INPUT_KEY_DEFAULT_INFO();
    input_key_service_cfg_t input_cfg = INPUT_KEY_SERVICE_DEFAULT_CONFIG();
    input_cfg.based_cfg.task_stack = (6 * 1024); // We need a bit more stack for the file explorer
    input_cfg.handle = set;
    periph_service_handle_t input_ser = input_key_service_create(&input_cfg);
    input_key_service_add_key(input_ser, input_key_info, INPUT_KEY_NUM);
    periph_service_set_callback(input_ser, input_key_service_cb, (void *)board_handle);

    ESP_LOGI(TAG, "[3.7] Create bt peripheral");
    esp_periph_handle_t bt_periph = bt_create_periph();

    ESP_LOGI(TAG, "[3.8] Start bt peripheral");
    esp_periph_start(set, bt_periph);

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = 0x3D,
        .control_phase_bytes = 1,               // According to SSD1306 datasheet
        .lcd_cmd_bits = 8,                      // According to SSD1306 datasheet
        .lcd_param_bits = 8,                    // According to SSD1306 datasheet
        .dc_bit_offset = 6,                     // According to SSD1306 datasheet
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)0, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install SSD1306 panel driver");
    esp_lcd_panel_handle_t panel_handle = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1,
        .reset_gpio_num = GPIO_NUM_21,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    ESP_LOGI(TAG, "Initialize LVGL");
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_port_init(&lvgl_cfg);

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = SSD1306_H_RES * SSD1306_V_RES,
        .double_buffer = true,
        .hres = SSD1306_H_RES,
        .vres = SSD1306_V_RES,
        .monochrome = true,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        }
    };
    lvgl_port_lock(0);
    lv_disp_t * disp = lvgl_port_add_disp(&disp_cfg);

    /* Rotation of the screen */
    lv_disp_set_rotation(disp, LV_DISP_ROT_NONE);
    lvgl_port_unlock();

    ui_common_init(disp);
    ui_mm_init();
    ui_np_init();
    ui_bt_init();
    ui_fe_init();

    while(1) {
#if configUSE_TRACE_FACILITY != 0
        heap_caps_print_heap_info(MALLOC_CAP_8BIT);
        esp_err_t ret = print_real_time_stats(100);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Oh Noooooo: %d", ret);
        }
#endif
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

#if configUSE_TRACE_FACILITY != 0
static esp_err_t print_real_time_stats(TickType_t xTicksToWait)
{
    TaskStatus_t *start_array = NULL, *end_array = NULL;
    UBaseType_t start_array_size, end_array_size;
    uint32_t start_run_time, end_run_time;
    esp_err_t ret;

    //Allocate array to store current task states
    start_array_size = uxTaskGetNumberOfTasks() + 10;
    start_array = malloc(sizeof(TaskStatus_t) * start_array_size);
    if (start_array == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto exit;
    }
    //Get current task states
    start_array_size = uxTaskGetSystemState(start_array, start_array_size, &start_run_time);
    if (start_array_size == 0) {
        ret = ESP_ERR_INVALID_SIZE;
        goto exit;
    }

    vTaskDelay(xTicksToWait);

    //Allocate array to store tasks states post delay
    end_array_size = uxTaskGetNumberOfTasks() + 10;
    end_array = malloc(sizeof(TaskStatus_t) * end_array_size);
    if (end_array == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto exit;
    }
    //Get post delay task states
    end_array_size = uxTaskGetSystemState(end_array, end_array_size, &end_run_time);
    if (end_array_size == 0) {
        ret = ESP_ERR_INVALID_SIZE;
        goto exit;
    }

    //Calculate total_elapsed_time in units of run time stats clock period.
    uint32_t total_elapsed_time = (end_run_time - start_run_time);
    if (total_elapsed_time == 0) {
        ret = ESP_ERR_INVALID_STATE;
        goto exit;
    }

    printf("|       Task       | RunTime | Percentage\n");
    //Match each task in start_array to those in the end_array
    for (int i = 0; i < start_array_size; i++) {
        int k = -1;
        for (int j = 0; j < end_array_size; j++) {
            if (start_array[i].xHandle == end_array[j].xHandle) {
                k = j;
                //Mark that task have been matched by overwriting their handles
                start_array[i].xHandle = NULL;
                end_array[j].xHandle = NULL;
                break;
            }
        }
        //Check if matching task found
        if (k >= 0) {
            uint32_t task_elapsed_time = end_array[k].ulRunTimeCounter - start_array[i].ulRunTimeCounter;
            uint32_t percentage_time = (task_elapsed_time * 100UL) / (total_elapsed_time * portNUM_PROCESSORS);
            printf("| %16s |  %6"PRIu32" | %2"PRIu32"%%\n", start_array[i].pcTaskName, task_elapsed_time, percentage_time);
        }
    }

    //Print unmatched tasks
    for (int i = 0; i < start_array_size; i++) {
        if (start_array[i].xHandle != NULL) {
            printf("| %16s | Deleted\n", start_array[i].pcTaskName);
        }
    }
    for (int i = 0; i < end_array_size; i++) {
        if (end_array[i].xHandle != NULL) {
            printf("| %16s | Created\n", end_array[i].pcTaskName);
        }
    }
    ret = ESP_OK;

exit:    //Common return path
    free(start_array);
    free(end_array);
    return ret;
}
#endif
