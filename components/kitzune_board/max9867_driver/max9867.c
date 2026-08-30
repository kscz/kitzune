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

#include "max9867.h"

static const char *TAG = "MAX9867";

static bool codec_init_flag = false;
static i2c_bus_handle_t i2c_handle;
static uint8_t s_volume = 25;

// k is the gain calibration reading used to scale it (V = 0.738 * AUX / k), typically ~19500
static int16_t s_aux_k = 0;
static int16_t s_aux_baseline = 0;

// Buttons on the in-line controls are just resistors to ground - these were measured on
// one kitzune and were wrong on another, so I need a better mechanism here
#define AUX_CTRL_CENTER_MV   775
#define AUX_CTRL_VOL_UP_MV   865
#define AUX_CTRL_VOL_DOWN_MV 938
#define AUX_CTRL_BAND_MV     35
#define AUX_CTRL_DEBOUNCE    2

static max9867_ctrl_t s_ctrl_stable = MAX9867_CTRL_NONE;
static max9867_ctrl_t s_ctrl_pending = MAX9867_CTRL_NONE;
static uint8_t s_ctrl_streak = 0;

static bool near_mv(int mv, int target)
{
    int d = mv - target;
    return (d < 0 ? -d : d) <= AUX_CTRL_BAND_MV;
}

static max9867_ctrl_t classify_mv(int mv)
{
    if (near_mv(mv, AUX_CTRL_CENTER_MV)) {
        return MAX9867_CTRL_CENTER;
    }
    if (near_mv(mv, AUX_CTRL_VOL_UP_MV)) {
        return MAX9867_CTRL_VOL_UP;
    }
    if (near_mv(mv, AUX_CTRL_VOL_DOWN_MV)) {
        return MAX9867_CTRL_VOL_DOWN;
    }
    return MAX9867_CTRL_NONE;
}

// You have to do a write, restart, read on the MAX9867 for a read to work
// this makes me a sad panda
static esp_err_t max9867_read_reg(uint8_t reg, uint8_t *out, size_t len)
{
    esp_err_t ret = ESP_OK;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (cmd == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ret |= i2c_master_start(cmd);
    ret |= i2c_master_write_byte(cmd, 0x30, true);
    ret |= i2c_master_write_byte(cmd, reg, true);
    ret |= i2c_master_start(cmd);
    ret |= i2c_master_write_byte(cmd, 0x30 | 0x01, true);
    if (len > 1) {
        ret |= i2c_master_read(cmd, out, len - 1, I2C_MASTER_ACK);
    }
    ret |= i2c_master_read_byte(cmd, &out[len - 1], I2C_MASTER_NACK);
    ret |= i2c_master_stop(cmd);

    if (ret == ESP_OK) {
        ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(1000));
    }
    i2c_cmd_link_delete(cmd);

    return ret;
}

// V = 0.738 * AUX / k, in mV
static int aux_to_mv(int16_t aux)
{
    if (s_aux_k <= 0) {
        return 0;
    }
    return (738 * (int)aux) / s_aux_k;
}

static int16_t read_aux_frozen(void)
{
    uint8_t regbuf, txbuf[1], aux_raw[2] = {0, 0};

    regbuf = 0x14;
    txbuf[0] = 0x9; // AUXEN = 1, AUXCAP = 1
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 1);

    max9867_read_reg(0x2, aux_raw, 2);

    regbuf = 0x14;
    txbuf[0] = 0x1; // AUXEN = 1, AUXCAP = 0
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 1);

    return (int16_t)(((uint16_t)aux_raw[0] << 8) | aux_raw[1]);
}

audio_hal_func_t AUDIO_MAX9867_DEFAULT_HANDLE = {
    .audio_codec_initialize = max9867_init,
    .audio_codec_deinitialize = max9867_deinit,
    .audio_codec_ctrl = max9867_ctrl_state,
    .audio_codec_config_iface = max9867_config_i2s,
    .audio_codec_set_mute = max9867_set_voice_mute,
    .audio_codec_set_volume = max9867_set_voice_volume,
    .audio_codec_get_volume = max9867_get_voice_volume,
};

bool max9867_initialized()
{
    return codec_init_flag;
}

esp_err_t max9867_init(audio_hal_codec_config_t *cfg)
{
    ESP_LOGI(TAG, "max9867 init");
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

    // Shut off the ADC
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
    txbuf[0] = 2; // Headphones set to capless, JDETEN = 0
    txbuf[1] = 0x80 | 0x3; // enable ADCs, !SHDN = 1
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 2);

    // Calibrate ADC offset
    ESP_LOGE(TAG, "Codec offset calibration");
    regbuf = 0x14;
    txbuf[0] = 0x3; // AUXEN = 1, AUXCAL = 1
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 1);

    vTaskDelay(pdMS_TO_TICKS(40));

    ESP_LOGE(TAG, "Codec offset calibration complete");
    regbuf = 0x14;
    txbuf[0] = 0x1; // AUXEN = 1, AUXCAL = 0
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 1);

    ESP_LOGE(TAG, "Codec gain calibration");
    regbuf = 0x14;
    txbuf[0] = 0x5; // AUXEN = 1, AUXGAIN = 1
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 1);

    vTaskDelay(pdMS_TO_TICKS(40));

    // Set AUXCAP to freeze result...
    regbuf = 0x14;
    txbuf[0] = 0xD; // AUXEN = 1, AUXGAIN = 1, AUXCAP = 1
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 1);

    // AUXGAIN must stay set while the result is frozen, so read it here
    uint8_t gain_result[2] = {0, 0};
    max9867_read_reg(0x2, gain_result, 2);
    s_aux_k = (int16_t)(((uint16_t)gain_result[0] << 8) | gain_result[1]);

    // End calibration!
    regbuf = 0x14;
    txbuf[0] = 0x1; // AUXEN = 1, AUXCAL = 0, AUXGAIN = 0, AUXCAP = 0
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 1);
    ESP_LOGI(TAG, "Codec gain calibration complete - k = %" PRId16, s_aux_k);
    if (s_aux_k <= 0) {
        ESP_LOGW(TAG, "Bad AUX gain calibration, DC measurements unavailable");
    }

    // Get a baseline reading for AUX
    ESP_LOGI(TAG, "Codec get base AUX reading");
    vTaskDelay(pdMS_TO_TICKS(40));

    s_aux_baseline = read_aux_frozen();

    ESP_LOGI(TAG, "Codec base AUX reading complete: %" PRId16 " (%d mV)",
             s_aux_baseline, aux_to_mv(s_aux_baseline));

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

esp_err_t max9867_deinit(void)
{
    return ESP_OK;
}

esp_err_t max9867_ctrl_state(audio_hal_codec_mode_t mode, audio_hal_ctrl_t ctrl_state)
{
    return ESP_OK;
}

esp_err_t max9867_config_i2s(audio_hal_codec_mode_t mode, audio_hal_codec_i2s_iface_t *iface)
{
    return ESP_OK;
}

esp_err_t max9867_set_voice_mute(bool mute)
{
    return ESP_OK;
}

esp_err_t max9867_set_voice_volume(int volume)
{
    uint8_t regbuf, txbuf[64];
    s_volume = (volume / 2);
    regbuf = 0x10;
    txbuf[0] = (50 - s_volume);
    txbuf[1] = (50 - s_volume);
    i2c_bus_write_bytes(i2c_handle, 0x30, &regbuf, 1, txbuf, 2);
    return ESP_OK;
}

esp_err_t max9867_get_voice_volume(int *volume)
{
    *volume = (s_volume * 2);
    return ESP_OK;
}

esp_err_t max9867_read_aux_mv(int *mv)
{
    if (mv == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!codec_init_flag || s_aux_k <= 0) {
        return ESP_ERR_INVALID_STATE;
    }

    *mv = aux_to_mv(read_aux_frozen());
    return ESP_OK;
}

esp_err_t max9867_get_aux_baseline_mv(int *mv)
{
    if (mv == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!codec_init_flag || s_aux_k <= 0) {
        return ESP_ERR_INVALID_STATE;
    }

    *mv = aux_to_mv(s_aux_baseline);
    return ESP_OK;
}

esp_err_t max9867_poll_inline_controls(max9867_ctrl_t *pressed)
{
    if (pressed == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *pressed = MAX9867_CTRL_NONE;

    if (!codec_init_flag || s_aux_k <= 0) {
        return ESP_ERR_INVALID_STATE;
    }

    int mv = aux_to_mv(read_aux_frozen());
    ESP_LOGD(TAG, "AUX %d mV", mv);

    // Require the same reading AUX_CTRL_DEBOUNCE times before believing it
    max9867_ctrl_t now = classify_mv(mv);
    if (now != s_ctrl_pending) {
        s_ctrl_pending = now;
        s_ctrl_streak = 1;
        return ESP_OK;
    }
    if (s_ctrl_streak < AUX_CTRL_DEBOUNCE) {
        s_ctrl_streak++;
        return ESP_OK;
    }

    // Report the edge into a button, but let release just re-arm
    if (s_ctrl_stable != s_ctrl_pending) {
        s_ctrl_stable = s_ctrl_pending;
        *pressed = s_ctrl_stable;
    }
    return ESP_OK;
}
