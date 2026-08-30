#pragma once

#include "esp_lcd_types.h"
#include "lvgl.h"

// Register a monochrome display whose flush only sends changed pages/columns.
// Call after lvgl_port_init() with the LVGL lock held. LV_DISP_ROT_NONE only.
// vres must be a multiple of 8.
lv_disp_t *mono_disp_add(esp_lcd_panel_handle_t panel, lv_coord_t hres, lv_coord_t vres);
