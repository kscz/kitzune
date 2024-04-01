#include "esp_bt.h"
#include "esp_gap_bt_api.h"

typedef struct {
    esp_bd_addr_t bda;
    uint32_t cod;
    uint8_t eir[ESP_BT_GAP_EIR_DATA_LEN];
    uint8_t bdname[ESP_BT_GAP_MAX_BDNAME_LEN + 1];
    uint8_t bdname_len;
    uint8_t eir_len;
    uint8_t rssi;
} bt_dev_info_t;

typedef void (*bt_be_disc_cb_t)(bt_dev_info_t *dev, size_t dev_count);

bool bt_be_is_discovery_complete(void);
esp_err_t bt_be_start_discovery(bt_be_disc_cb_t disc_comp_cb);
esp_err_t bt_be_connect_ad2p(esp_bd_addr_t bda);
void bt_be_init(void);


