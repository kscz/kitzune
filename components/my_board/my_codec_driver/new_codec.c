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

#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "board.h"
#include "i2c_bus.h"

#include "new_codec.h"

static const char *TAG = "new_codec";

static bool codec_init_flag = false;
static i2c_bus_handle_t i2c_handle;

audio_hal_func_t AUDIO_NEW_CODEC_DEFAULT_HANDLE = {
    .audio_codec_initialize = new_codec_init,
    .audio_codec_deinitialize = new_codec_deinit,
    .audio_codec_ctrl = new_codec_ctrl_state,
    .audio_codec_config_iface = new_codec_config_i2s,
    .audio_codec_set_mute = new_codec_set_voice_mute,
    .audio_codec_set_volume = new_codec_set_voice_volume,
    .audio_codec_get_volume = new_codec_get_voice_volume,
};

bool new_codec_initialized()
{
    return codec_init_flag;
}

esp_err_t new_codec_init(audio_hal_codec_config_t *cfg)
{
    ESP_LOGI(TAG, "new_codec init");
    int res = 0;
    i2c_config_t max_i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_pullup_en = 0,
        .scl_pullup_en = 0,
        .master.clk_speed = 100000,
    };
    res = get_i2c_pins(I2C_NUM_0, &max_i2c_cfg);
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "i2c pin config error");
    }
    i2c_handle = i2c_bus_create(I2C_NUM_0, &max_i2c_cfg);

    uint8_t regbuf, txbuf[64];
    ESP_LOGE(TAG, "Codec shutdown");
    // Force the device into shutdown and disable the DACs
    regbuf = 0x17;
    txbuf[0] = 0;
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 1);

    // Configure codec clock
    ESP_LOGE(TAG, "Codec Clock cfg");
    regbuf = 0x05;
    txbuf[0] = (1 << 4);
    txbuf[1] = 0x80;
    txbuf[2] = 0x00;
    txbuf[3] = 0x10;
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 4);

    // Configure volume
    ESP_LOGE(TAG, "Codec Volume");
    regbuf = 0x10;
    txbuf[0] = 0x20;
    txbuf[1] = 0x20;
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 2);

    // Configure headphone amplifier mode, take device out of shutdown, enable dacs
    ESP_LOGE(TAG, "Codec ACTIVATE");
    regbuf = 0x16;
    txbuf[0] = 2;
    txbuf[1] = (1 << 7) | (0x3 << 2);
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 2);

    codec_init_flag  = true;

    return ESP_OK;
}

esp_err_t new_codec_deinit(void)
{
    return ESP_OK;
}

esp_err_t new_codec_ctrl_state(audio_hal_codec_mode_t mode, audio_hal_ctrl_t ctrl_state)
{
    return ESP_OK;
}

esp_err_t new_codec_config_i2s(audio_hal_codec_mode_t mode, audio_hal_codec_i2s_iface_t *iface)
{
    return ESP_OK;
}

esp_err_t new_codec_set_voice_mute(bool mute)
{
    return ESP_OK;
}

esp_err_t new_codec_set_voice_volume(int volume)
{
    return ESP_OK;
}

esp_err_t new_codec_get_voice_volume(int *volume)
{
    return ESP_OK;
}
