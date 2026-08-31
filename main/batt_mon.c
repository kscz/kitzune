#include "esp_err.h"
#include "esp_log.h"

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "batt_mon.h"

static const char *TAG = "BATT";

// GPIO35
#define BATT_ADC_UNIT     ADC_UNIT_1
#define BATT_ADC_CHANNEL  ADC_CHANNEL_7
#define BATT_ADC_ATTEN    ADC_ATTEN_DB_12

// 200k / 200k divider
#define BATT_DIV_NUM      2
#define BATT_DIV_DEN      1

#define BATT_SAMPLES      16
#define BATT_DEFAULT_VREF 1100

// New reading gets 1/BATT_EMA_DIV of the weight
#define BATT_EMA_DIV      4

static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t s_cali = NULL;
static int s_filt_mv = 0;

static const struct {
    int mv;
    uint8_t pct;
} s_curve[] = {
    {4200, 100},
    {4100,  92},
    {4000,  85},
    {3950,  78},
    {3900,  70},
    {3850,  62},
    {3800,  55},
    {3750,  48},
    {3700,  42},
    {3650,  35},
    {3600,  28},
    {3550,  20},
    {3500,  14},
    {3400,   8},
    {3300,   4},
    {3000,   0},
};

#define BATT_CURVE_LEN (sizeof(s_curve) / sizeof(s_curve[0]))

static uint8_t batt_mv_to_pct(int mv)
{
    if (mv >= s_curve[0].mv) {
        return s_curve[0].pct;
    }

    for (int i = 1; i < BATT_CURVE_LEN; i++) {
        if (mv >= s_curve[i].mv) {
            int span_mv = s_curve[i - 1].mv - s_curve[i].mv;
            int span_pct = s_curve[i - 1].pct - s_curve[i].pct;
            return s_curve[i].pct + ((mv - s_curve[i].mv) * span_pct) / span_mv;
        }
    }

    return 0;
}

esp_err_t batt_mon_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = BATT_ADC_UNIT,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    esp_err_t ret = adc_oneshot_new_unit(&unit_cfg, &s_adc);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to claim the ADC unit: %s", esp_err_to_name(ret));
        return ret;
    }

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = BATT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_oneshot_config_channel(s_adc, BATT_ADC_CHANNEL, &chan_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to configure the ADC channel: %s", esp_err_to_name(ret));
        adc_oneshot_del_unit(s_adc);
        s_adc = NULL;
        return ret;
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = BATT_ADC_UNIT,
        .chan = BATT_ADC_CHANNEL,
        .atten = BATT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {
        .unit_id = BATT_ADC_UNIT,
        .atten = BATT_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
#if CONFIG_IDF_TARGET_ESP32
        .default_vref = BATT_DEFAULT_VREF,
#endif
    };
    ret = adc_cali_create_scheme_line_fitting(&cali_cfg, &s_cali);
#else
    ret = ESP_ERR_NOT_SUPPORTED;
#endif
    if (ret != ESP_OK) {
        // Raw counts still give a usable trend, so keep going uncalibrated
        ESP_LOGW(TAG, "No ADC calibration available: %s", esp_err_to_name(ret));
        s_cali = NULL;
    }

    return ESP_OK;
}

esp_err_t batt_mon_poll(void)
{
    if (s_adc == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int sum = 0;
    int taken = 0;
    for (int i = 0; i < BATT_SAMPLES; i++) {
        int raw = 0;
        if (ESP_OK == adc_oneshot_read(s_adc, BATT_ADC_CHANNEL, &raw)) {
            sum += raw;
            taken++;
        }
    }

    if (taken == 0) {
        ESP_LOGW(TAG, "No ADC samples landed");
        return ESP_FAIL;
    }

    int raw_avg = sum / taken;
    int adc_mv = 0;
    if (s_cali != NULL) {
        esp_err_t ret = adc_cali_raw_to_voltage(s_cali, raw_avg, &adc_mv);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Unable to convert the ADC reading: %s", esp_err_to_name(ret));
            return ret;
        }
    } else {
        // 12dB attenuation tops out near 2450mV across the 12-bit range
        adc_mv = (raw_avg * 2450) / 4095;
    }

    int cell_mv = (adc_mv * BATT_DIV_NUM) / BATT_DIV_DEN;

    if (s_filt_mv == 0) {
        s_filt_mv = cell_mv;
    } else {
        s_filt_mv += (cell_mv - s_filt_mv) / BATT_EMA_DIV;
    }

    ESP_LOGI(TAG, "Cell %d mV (raw %d, avg %d mV) -> %u%%",
             s_filt_mv, raw_avg, cell_mv, batt_mv_to_pct(s_filt_mv));

    return ESP_OK;
}

int batt_mon_get_mv(void)
{
    return s_filt_mv;
}

uint8_t batt_mon_get_percent(void)
{
    if (s_filt_mv == 0) {
        return 100;
    }

    return batt_mv_to_pct(s_filt_mv);
}
