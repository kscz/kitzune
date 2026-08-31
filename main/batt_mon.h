#include "esp_err.h"

esp_err_t batt_mon_init(void);
esp_err_t batt_mon_poll(void);

// 0 until the first successful poll
int batt_mon_get_mv(void);
uint8_t batt_mon_get_percent(void);
