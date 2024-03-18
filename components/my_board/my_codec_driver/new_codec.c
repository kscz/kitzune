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
static uint8_t s_volume = 25;

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
        .master.clk_speed = 400000,
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

    // Calibrate ADC offset
    regbuf = 0x14;
    txbuf[0] = 0x0;
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 1);

    // Configure codec clock fixed at 12.288MHz MCLK, 48kHz LRCLK
    ESP_LOGE(TAG, "Codec Clock initial cfg");
    regbuf = 0x05;
    txbuf[0] = (1 << 4);
    txbuf[1] = 0x60; // PLL disabled, NI = 0x6000
    txbuf[2] = 0x00;
    txbuf[3] = 0x10; // Slave mode, I2S compatible signal
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 4);

    // Diable JDETEN
    ESP_LOGE(TAG, "Codec disable JDETEN, enable ADCs, calibration start");
    regbuf = 0x16;
    txbuf[0] = 2;
    txbuf[1] = 0x80 | 0x3; // enable ADCs, !SHDN = 1
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 2);

    // Calibrate ADC offset
    ESP_LOGE(TAG, "Codec offset calibration");
    regbuf = 0x14;
    txbuf[0] = 0x3; // AUXEN = 1, AUXCAL = 1
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 1);

    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_LOGE(TAG, "Codec offset calibration complete");
    regbuf = 0x14;
    txbuf[0] = 0x0; // AUXEN = 0, AUXCAL = 0
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 1);

    ESP_LOGE(TAG, "Codec gain calibration");
    regbuf = 0x14;
    txbuf[0] = 0x5; // AUXEN = 1, AUXGAIN = 1
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 1);

    vTaskDelay(pdMS_TO_TICKS(20));

    // Set AUXCAP to freeze result...
    regbuf = 0x14;
    txbuf[0] = 0xD; // AUXEN = 1, AUXGAIN = 1, AUXCAP = 1
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 1);

    regbuf = 0x2;
    uint8_t gain_result[2] = {0, 0};
    i2c_bus_read_bytes(i2c_handle, 0x30, &regbuf, 1, gain_result, 2);

    // End calibration!
    regbuf = 0x14;
    txbuf[0] = 0x1; // AUXEN = 1, AUXCAL = 0, AUXGAIN = 0, AUXCAP = 0
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 1);
    ESP_LOGE(TAG, "Codec gain calibration complete - 0x%" PRIx8 "%" PRIx8, gain_result[0], gain_result[1]);

    // Get a baseline reading for AUX
    ESP_LOGE(TAG, "Codec get base AUX reading");
    vTaskDelay(pdMS_TO_TICKS(20));

    // Freeze base reading
    regbuf = 0x14;
    txbuf[0] = 0x9; // AUXEN = 1, AUXCAP = 1
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 1);

    // Read AUX register
    regbuf = 0x2;
    uint8_t aux_result[2] = {0, 0};
    i2c_bus_read_bytes(i2c_handle, 0x30, &regbuf, 1, gain_result, 2);

    regbuf = 0x14;
    txbuf[0] = 0x1; // AUXEN = 1
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 1);

    ESP_LOGE(TAG, "Codec get base AUX reading complete: 0x%" PRIx8 "%" PRIx8, aux_result[0], aux_result[1]);

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
    txbuf[0] = (50 - s_volume);
    txbuf[1] = (50 - s_volume);
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 2);

    // Configure the microphone
    ESP_LOGE(TAG, "Codec Mic Enable");
    regbuf = 0x12;
    txbuf[0] = (1 << 5);
    txbuf[1] = (0);
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 2);

    // Configure headphone amplifier mode, take device out of shutdown, enable dacs
    ESP_LOGE(TAG, "Codec ACTIVATE");
    regbuf = 0x16;
    txbuf[0] = (1 << 3) | 2;
    txbuf[1] = (1 << 7) | (0x3 << 2) | (0x3);
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
    uint8_t regbuf, txbuf[64];
    s_volume = (volume / 2);
    regbuf = 0x10;
    txbuf[0] = (50 - s_volume);
    txbuf[1] = (50 - s_volume);
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 2);
    return ESP_OK;
}

esp_err_t new_codec_get_voice_volume(int *volume)
{
    *volume = (s_volume * 2);
    return ESP_OK;
}
