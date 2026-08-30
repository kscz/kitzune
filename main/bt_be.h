#include "esp_bt.h"
#include "esp_gap_bt_api.h"

// Max bonded devices we list
#define BT_BE_MAX_PAIRED 8

// Remembered names are display-only, so they're clipped short
#define BT_BE_NAME_LEN 33

typedef struct {
    esp_bd_addr_t bda;
    uint32_t cod;
    uint8_t eir[ESP_BT_GAP_EIR_DATA_LEN];
    uint8_t bdname[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
    uint8_t bdname_len;
    uint8_t eir_len;
    uint8_t rssi;
} bt_dev_info_t;

typedef struct {
    esp_bd_addr_t bda;
    char name[BT_BE_NAME_LEN];
} bt_paired_dev_t;

typedef void (*bt_be_disc_cb_t)(bt_dev_info_t *dev, size_t dev_count);

bool bt_be_is_discovery_complete(void);
esp_err_t bt_be_start_discovery(bt_be_disc_cb_t disc_comp_cb);

// Currently bonded devices with their remembered names. Returns entries written.
size_t bt_be_get_paired_devices(bt_paired_dev_t *out, size_t max);

// Drop every bond and the names that went with them
esp_err_t bt_be_forget_devices(void);

// name may be NULL/empty; when given it's remembered for the paired list
esp_err_t bt_be_connect_ad2p(esp_bd_addr_t bda, const char *name);
void bt_be_init(void);

bool bt_be_is_enabled(void);

// Only bring down bluedroid/controller; a2dp, avrc and the bt peripheral must
// already be gone. Use player_be_disable_bt() instead of calling this directly.
esp_err_t bt_be_deinit(void);


