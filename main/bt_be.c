#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_gap_bt_api.h"

#include "bt_be.h"

#define TAG "BT_BE"

// The stack stores bonds but not names, so we shadow the names here
#define BT_NVS_NAMESPACE "kz_bt_pair"
#define BDA_KEY_LEN 13
#define BT_BE_MAX_BONDS 16

typedef enum {
    APP_GAP_STATE_IDLE = 0,
    APP_GAP_STATE_DEVICE_DISCOVERING,
    APP_GAP_STATE_DEVICE_DISCOVER_COMPLETE,
    APP_GAP_STATE_SERVICE_DISCOVERING,
    APP_GAP_STATE_SERVICE_DISCOVER_COMPLETE,
} app_gap_state_t;

static bt_dev_info_t s_dev[16];
static size_t s_dev_count = 0;
static app_gap_state_t s_state;
static bt_be_disc_cb_t s_disc_complete_cb = NULL;
static bool s_enabled = false;

static char *bda2str(esp_bd_addr_t bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }

    uint8_t *p = bda;
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            p[0], p[1], p[2], p[3], p[4], p[5]);
    return str;
}

static void bda2key(const uint8_t *bda, char *key)
{
    sprintf(key, "%02x%02x%02x%02x%02x%02x",
            bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
}

// Best effort: a name we can't store just means the bond shows as an address
static void save_device_name(const uint8_t *bda, const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return;
    }

    char key[BDA_KEY_LEN];
    bda2key(bda, key);

    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(BT_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Unable to open %s: %s", BT_NVS_NAMESPACE, esp_err_to_name(ret));
        return;
    }

    // Skip the write if it wouldn't change anything
    char existing[BT_BE_NAME_LEN];
    size_t existing_len = sizeof(existing);
    if (nvs_get_str(nvs, key, existing, &existing_len) == ESP_OK &&
            strncmp(existing, name, BT_BE_NAME_LEN - 1) == 0) {
        nvs_close(nvs);
        return;
    }

    char trimmed[BT_BE_NAME_LEN];
    snprintf(trimmed, sizeof(trimmed), "%s", name);
    if ((ret = nvs_set_str(nvs, key, trimmed)) == ESP_OK) {
        ret = nvs_commit(nvs);
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Unable to store name for %s: %s", key, esp_err_to_name(ret));
    }
    nvs_close(nvs);
}

// Drop names whose bond the stack no longer has
static void prune_stale_names(nvs_handle_t nvs, const esp_bd_addr_t *bonded, size_t bonded_count)
{
    char stale[BT_BE_MAX_PAIRED][NVS_KEY_NAME_MAX_SIZE];
    size_t stale_count = 0;

    nvs_iterator_t it = NULL;
    esp_err_t ret = nvs_entry_find_in_handle(nvs, NVS_TYPE_STR, &it);
    while (ret == ESP_OK && stale_count < BT_BE_MAX_PAIRED) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);

        bool is_bonded = false;
        for (size_t i = 0; i < bonded_count && !is_bonded; ++i) {
            char key[BDA_KEY_LEN];
            bda2key(bonded[i], key);
            is_bonded = (strcmp(info.key, key) == 0);
        }

        if (!is_bonded) {
            strlcpy(stale[stale_count], info.key, NVS_KEY_NAME_MAX_SIZE);
            stale_count += 1;
        }

        ret = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);

    for (size_t i = 0; i < stale_count; ++i) {
        ESP_LOGI(TAG, "Forgetting unbonded device %s", stale[i]);
        nvs_erase_key(nvs, stale[i]);
    }
    if (stale_count != 0) {
        nvs_commit(nvs);
    }
}

size_t bt_be_get_paired_devices(bt_paired_dev_t *out, size_t max)
{
    if (!s_enabled || out == NULL || max == 0) {
        return 0;
    }

    int bond_count = (max < BT_BE_MAX_PAIRED) ? (int)max : BT_BE_MAX_PAIRED;
    esp_bd_addr_t bonded[BT_BE_MAX_PAIRED];
    esp_err_t ret = esp_bt_gap_get_bond_device_list(&bond_count, bonded);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Unable to read the bond list: %s", esp_err_to_name(ret));
        return 0;
    }
    if (bond_count <= 0) {
        return 0;
    }

    nvs_handle_t nvs;
    bool have_nvs = (nvs_open(BT_NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK);

    for (int i = 0; i < bond_count; ++i) {
        memcpy(out[i].bda, bonded[i], sizeof(out[i].bda));
        out[i].name[0] = '\0';

        if (have_nvs) {
            char key[BDA_KEY_LEN];
            bda2key(bonded[i], key);
            size_t name_len = sizeof(out[i].name);
            if (nvs_get_str(nvs, key, out[i].name, &name_len) != ESP_OK) {
                out[i].name[0] = '\0';
            }
        }

        // Fall back to the address for a bond we never caught the name of
        if (out[i].name[0] == '\0') {
            bda2str(bonded[i], out[i].name, sizeof(out[i].name));
        }
    }

    if (have_nvs) {
        prune_stale_names(nvs, bonded, bond_count);
        nvs_close(nvs);
    }

    return (size_t)bond_count;
}

esp_err_t bt_be_forget_devices(void)
{
    if (!s_enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    // The stack can hold more bonds than we list
    int bond_count = esp_bt_gap_get_bond_device_num();
    if (bond_count <= 0) {
        return ESP_OK;
    }
    if (bond_count > BT_BE_MAX_BONDS) {
        bond_count = BT_BE_MAX_BONDS;
    }

    esp_bd_addr_t bonded[BT_BE_MAX_BONDS];
    esp_err_t ret = esp_bt_gap_get_bond_device_list(&bond_count, bonded);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Unable to read the bond list: %s", esp_err_to_name(ret));
        return ret;
    }

    char bda_str[18];
    for (int i = 0; i < bond_count; ++i) {
        esp_err_t rm_ret = esp_bt_gap_remove_bond_device(bonded[i]);
        if (rm_ret != ESP_OK) {
            ESP_LOGW(TAG, "Unable to drop bond %s: %s",
                     bda2str(bonded[i], bda_str, sizeof(bda_str)), esp_err_to_name(rm_ret));
            ret = rm_ret;
        }
    }

    nvs_handle_t nvs;
    if (nvs_open(BT_NVS_NAMESPACE, NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_erase_all(nvs);
        nvs_commit(nvs);
        nvs_close(nvs);
    }

    ESP_LOGI(TAG, "Dropped %d bonds", bond_count);
    return ret;
}

esp_err_t bt_be_connect_ad2p(esp_bd_addr_t bda, const char *name) {
    if (!s_enabled) {
        return ESP_ERR_INVALID_STATE;
    }

    // Stale entries get pruned if the bond never lands
    save_device_name(bda, name);

    char bda_str[18];
    ESP_LOGI(TAG, "Connecting: %s", bda2str(bda, bda_str, 18));
    return esp_a2d_source_connect(bda);
}

static bool get_name_from_eir(uint8_t *eir, uint8_t *bdname, uint8_t *bdname_len)
{
    uint8_t *rmt_bdname = NULL;
    uint8_t rmt_bdname_len = 0;

    if (!eir) {
        return false;
    }

    rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_bdname_len);
    if (!rmt_bdname) {
        rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_bdname_len);
    }

    if (rmt_bdname) {
        if (rmt_bdname_len > ESP_BT_GAP_MAX_BDNAME_LEN) {
            rmt_bdname_len = ESP_BT_GAP_MAX_BDNAME_LEN;
        }

        if (bdname) {
            memcpy(bdname, rmt_bdname, rmt_bdname_len);
            bdname[rmt_bdname_len] = '\0';
        }
        if (bdname_len) {
            *bdname_len = rmt_bdname_len;
        }
        return true;
    }

    return false;
}

static void update_device_info(esp_bt_gap_cb_param_t *param)
{
    char bda_str[18];
    uint32_t cod = 0;
    int32_t rssi = -129; /* invalid value */
    uint8_t *bdname = NULL;
    uint8_t bdname_len = 0;
    uint8_t *eir = NULL;
    uint8_t eir_len = 0;
    esp_bt_gap_dev_prop_t *p;

    ESP_LOGI(TAG, "Device found: %s", bda2str(param->disc_res.bda, bda_str, 18));
    for (int i = 0; i < param->disc_res.num_prop; i++) {
        p = param->disc_res.prop + i;
        switch (p->type) {
        case ESP_BT_GAP_DEV_PROP_COD:
            cod = *(uint32_t *)(p->val);
            ESP_LOGI(TAG, "--Class of Device: 0x%"PRIx32, cod);
            break;
        case ESP_BT_GAP_DEV_PROP_RSSI:
            rssi = *(int8_t *)(p->val);
            ESP_LOGI(TAG, "--RSSI: %"PRId32, rssi);
            break;
        case ESP_BT_GAP_DEV_PROP_BDNAME:
            bdname_len = (p->len > ESP_BT_GAP_MAX_BDNAME_LEN) ? ESP_BT_GAP_MAX_BDNAME_LEN :
                          (uint8_t)p->len;
            bdname = (uint8_t *)(p->val);
            break;
        case ESP_BT_GAP_DEV_PROP_EIR: {
            eir_len = p->len;
            eir = (uint8_t *)(p->val);
            break;
        }
        default:
            break;
        }
    }

    // We filter out all devices which aren't "AV" devices
    if (!esp_bt_gap_is_valid_cod(cod) ||
             esp_bt_gap_get_cod_major_dev(cod) != ESP_BT_COD_MAJOR_DEV_AV) {
        return;
    }

    // Confirm there's enough space to hold more results
    if (s_dev_count >= sizeof(s_dev)/sizeof(*s_dev)) {
        ESP_LOGE(TAG, "Discovered device, no space remaining");
        return;
    }

    // Check that we haven't already discovered this device
    for (size_t i = 0; i < s_dev_count; ++i) {
        if (memcmp(param->disc_res.bda, s_dev[i].bda, ESP_BD_ADDR_LEN) == 0) {
            ESP_LOGI(TAG, "Ignoring duplicate device");
            return;
        }
    }

    bt_dev_info_t *p_dev = &s_dev[s_dev_count];
    s_dev_count += 1;

    memcpy(p_dev->bda, param->disc_res.bda, ESP_BD_ADDR_LEN);

    p_dev->cod = cod;
    p_dev->rssi = rssi;
    if (bdname_len > 0) {
        memcpy(p_dev->bdname, bdname, bdname_len);
        p_dev->bdname[bdname_len] = '\0';
        p_dev->bdname_len = bdname_len;
    }
    if (eir_len > 0) {
        memcpy(p_dev->eir, eir, eir_len);
        p_dev->eir_len = eir_len;
    }

    if (p_dev->bdname_len == 0) {
        get_name_from_eir(p_dev->eir, p_dev->bdname, &p_dev->bdname_len);
    }

    ESP_LOGI(TAG, "Found a target device, address %s, name %s", bda_str, p_dev->bdname);
}

static void bt_app_gap_init(void)
{
    s_dev_count = 0;
    s_state = APP_GAP_STATE_IDLE;
}

static void bt_app_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT: {
        update_device_info(param);
        break;
    }
    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            ESP_LOGI(TAG, "Device discovery stopped.");
            s_state = APP_GAP_STATE_DEVICE_DISCOVER_COMPLETE;
            if (s_disc_complete_cb != NULL) {
                s_disc_complete_cb(s_dev, s_dev_count);
            }
        } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
            ESP_LOGI(TAG, "Discovery started.");
        }
        break;
    }
    case ESP_BT_GAP_PIN_REQ_EVT: {
            ESP_LOGI(TAG, "ESP_BT_GAP_PIN_REQ_EVT min_16_digit:%d", param->pin_req.min_16_digit);
            if (param->pin_req.min_16_digit) {
                ESP_LOGI(TAG, "Attempting 16 digit pin: 0000 0000 0000 0000");
                esp_bt_pin_code_t pin_code = {0};
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
            } else {
                esp_bt_pin_code_t pin_code = {'1', '2', '3', '4'};
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
            }
            break;
        }
    case ESP_BT_GAP_AUTH_CMPL_EVT: {
            if (param->auth_cmpl.stat != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(TAG, "Authentication failed, status: %d", param->auth_cmpl.stat);
                break;
            }

            // device_name isn't guaranteed terminated, so bound the copy
            char name[BT_BE_NAME_LEN];
            snprintf(name, sizeof(name), "%.*s", (int)(sizeof(name) - 1),
                     (const char *)param->auth_cmpl.device_name);
            ESP_LOGI(TAG, "Authenticated: %s", name);
            save_device_name(param->auth_cmpl.bda, name);
            break;
        }
    case ESP_BT_GAP_MODE_CHG_EVT:
            ESP_LOGI(TAG, "ESP_BT_GAP_MODE_CHG_EVT mode:%d", param->mode_chg.mode);
            break;
    default: {
        ESP_LOGI(TAG, "GAP event: %d", event);
        break;
    }
    }
    return;
}

bool bt_be_is_discovery_complete(void) {
    return (s_state == APP_GAP_STATE_DEVICE_DISCOVER_COMPLETE);
}

esp_err_t bt_be_start_discovery(bt_be_disc_cb_t disc_comp_cb) {
    if (!s_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_state != APP_GAP_STATE_DEVICE_DISCOVER_COMPLETE && s_state != APP_GAP_STATE_IDLE) {
        return ESP_FAIL;
    }
    // inititialize device information and status
    bt_app_gap_init();

    s_disc_complete_cb = disc_comp_cb;

    // start to discover nearby Bluetooth devices
    s_state = APP_GAP_STATE_DEVICE_DISCOVERING;
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);

    // set discoverable and connectable mode, wait to be connected
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);

    return ESP_OK;
}

void bt_be_init(void)
{
    esp_err_t ret;
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if ((ret = esp_bt_controller_init(&bt_cfg)) != ESP_OK) {
        ESP_LOGE(TAG, "%s initialize controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) {
        ESP_LOGE(TAG, "%s enable controller failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bredr_tx_power_set(ESP_PWR_LVL_N6, ESP_PWR_LVL_N0)) != ESP_OK) {
        ESP_LOGE(TAG, "%s failed to set power limits: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bluedroid_init()) != ESP_OK) {
        ESP_LOGE(TAG, "%s initialize bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    if ((ret = esp_bluedroid_enable()) != ESP_OK) {
        ESP_LOGE(TAG, "%s enable bluedroid failed: %s\n", __func__, esp_err_to_name(ret));
        return;
    }

    /* set default parameters for Secure Simple Pairing */
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_IO;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

    /*
     * Set default parameters for Legacy Pairing
     * Use variable pin, input pin code when pairing
     */
    esp_bt_pin_type_t pin_type = ESP_BT_PIN_TYPE_VARIABLE;
    esp_bt_pin_code_t pin_code;
    esp_bt_gap_set_pin(pin_type, 0, pin_code);

    /* register GAP callback function */
    esp_bt_gap_register_callback(bt_app_gap_cb);

    esp_bt_gap_set_device_name("KITZUNE");

    s_enabled = true;
}

bool bt_be_is_enabled(void) {
    return s_enabled;
}

esp_err_t bt_be_deinit(void)
{
    esp_err_t ret;

    if (!s_enabled) {
        return ESP_ERR_INVALID_STATE;
    }
    s_enabled = false;

    // Drop the callback before cancelling: the stop event fires on the BTC task
    s_disc_complete_cb = NULL;
    if (s_state == APP_GAP_STATE_DEVICE_DISCOVERING) {
        esp_bt_gap_cancel_discovery();
    }
    s_state = APP_GAP_STATE_IDLE;
    s_dev_count = 0;

    if ((ret = esp_bluedroid_disable()) != ESP_OK) {
        ESP_LOGE(TAG, "%s bluedroid disable failed: %s", __func__, esp_err_to_name(ret));
        return ret;
    }
    if ((ret = esp_bluedroid_deinit()) != ESP_OK) {
        ESP_LOGE(TAG, "%s bluedroid deinit failed: %s", __func__, esp_err_to_name(ret));
        return ret;
    }
    if ((ret = esp_bt_controller_disable()) != ESP_OK) {
        ESP_LOGE(TAG, "%s controller disable failed: %s", __func__, esp_err_to_name(ret));
        return ret;
    }
    if ((ret = esp_bt_controller_deinit()) != ESP_OK) {
        ESP_LOGE(TAG, "%s controller deinit failed: %s", __func__, esp_err_to_name(ret));
        return ret;
    }

    // One-way: drop this to make BT re-enablable without a reboot
    if ((ret = esp_bt_mem_release(ESP_BT_MODE_CLASSIC_BT)) != ESP_OK) {
        ESP_LOGE(TAG, "%s memory release failed: %s", __func__, esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Bluetooth torn down");
    return ESP_OK;
}
