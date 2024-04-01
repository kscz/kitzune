/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2020 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef _AUDIO_BOARD_DEFINITION_H_
#define _AUDIO_BOARD_DEFINITION_H_

#define BUTTON_UP_ID           34
#define BUTTON_DOWN_ID         36
#define BUTTON_CENTER_ID       39
#define BUTTON_LEFT_ID         38
#define BUTTON_RIGHT_ID        37

#define BUTTON_VOLUP_ID           BUTTON_UP_ID
#define BUTTON_VOLDOWN_ID         BUTTON_DOWN_ID
#define BUTTON_SET_ID             BUTTON_LEFT_ID
#define BUTTON_MODE_ID            BUTTON_RIGHT_ID
#define BUTTON_PLAY_ID            BUTTON_CENTER_ID
#define SDCARD_INTR_GPIO          27

#define SDCARD_OPEN_FILE_NUM_MAX  5

#define BOARD_PA_GAIN             (10) /* Power amplifier gain defined by board (dB) */

#define SDCARD_PWR_CTRL           -1
#define ESP_SD_PIN_CLK             14
#define ESP_SD_PIN_CMD             15
#define ESP_SD_PIN_D0              2
#define ESP_SD_PIN_D1              4
#define ESP_SD_PIN_D2              12
#define ESP_SD_PIN_D3              13
#define ESP_SD_PIN_D4             -1
#define ESP_SD_PIN_D5             -1
#define ESP_SD_PIN_D6             -1
#define ESP_SD_PIN_D7             -1
#define ESP_SD_PIN_CD             -1
#define ESP_SD_PIN_WP             -1

extern audio_hal_func_t AUDIO_MAX9867_DEFAULT_HANDLE;

#define AUDIO_CODEC_DEFAULT_CONFIG(){                   \
        .adc_input  = AUDIO_HAL_ADC_INPUT_LINE1,        \
        .dac_output = AUDIO_HAL_DAC_OUTPUT_ALL,         \
        .codec_mode = AUDIO_HAL_CODEC_MODE_DECODE,      \
        .i2s_iface = {                                  \
            .mode = AUDIO_HAL_MODE_MASTER,              \
            .fmt = AUDIO_HAL_I2S_NORMAL,                \
            .samples = AUDIO_HAL_48K_SAMPLES,           \
            .bits = AUDIO_HAL_BIT_LENGTH_16BITS,        \
        },                                              \
};

#define INPUT_KEY_NUM     5

#define INPUT_KEY_USER_ID_UP     0x10
#define INPUT_KEY_USER_ID_DOWN   0x11
#define INPUT_KEY_USER_ID_LEFT   0x12
#define INPUT_KEY_USER_ID_RIGHT  0x13
#define INPUT_KEY_USER_ID_CENTER 0x14

#define INPUT_KEY_DEFAULT_INFO() {                      \
    {                                                   \
        .type = PERIPH_ID_BUTTON,                       \
        .user_id = INPUT_KEY_USER_ID_UP,                \
        .act_id = BUTTON_UP_ID,                         \
    },                                                  \
    {                                                   \
        .type = PERIPH_ID_BUTTON,                       \
        .user_id = INPUT_KEY_USER_ID_DOWN,              \
        .act_id = BUTTON_DOWN_ID,                       \
    },                                                  \
    {                                                   \
        .type = PERIPH_ID_BUTTON,                       \
        .user_id = INPUT_KEY_USER_ID_CENTER,            \
        .act_id = BUTTON_CENTER_ID,                     \
    },                                                  \
    {                                                   \
        .type = PERIPH_ID_BUTTON,                       \
        .user_id = INPUT_KEY_USER_ID_LEFT,              \
        .act_id = BUTTON_LEFT_ID,                       \
    },                                                  \
    {                                                   \
        .type = PERIPH_ID_BUTTON,                       \
        .user_id = INPUT_KEY_USER_ID_RIGHT,             \
        .act_id = BUTTON_RIGHT_ID,                      \
    },                                                  \
}

#endif
