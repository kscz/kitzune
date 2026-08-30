#include <stdlib.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lcd_panel_ops.h"

#include "lvgl.h"
#include "mono_disp.h"

#define TAG "MONO_DISP"

// An SSD1306 page is 8 stacked rows sharing one byte
#define PAGE_HEIGHT 8

typedef struct {
    esp_lcd_panel_handle_t panel;
    lv_coord_t hres;
    lv_coord_t vres;
    lv_disp_drv_t drv;
    lv_disp_draw_buf_t draw_buf;
    uint8_t *shadow;
    bool shadow_valid;
} mono_disp_t;

// Pack pixels into SSD1306 GDDRAM order so the flush can hand out raw slices
static void mono_disp_set_px(lv_disp_drv_t *drv, uint8_t *buf, lv_coord_t buf_w,
                             lv_coord_t x, lv_coord_t y, lv_color_t color, lv_opa_t opa) {
    LV_UNUSED(buf_w);
    LV_UNUSED(opa);

    buf += drv->hor_res * (y / PAGE_HEIGHT) + x;
    if (lv_color_to1(color)) {
        (*buf) &= ~(1 << (y % PAGE_HEIGHT));
    } else {
        (*buf) |= (1 << (y % PAGE_HEIGHT));
    }
}

// Send only the columns of each page that differ from what the panel shows
static void mono_disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
    mono_disp_t *ctx = (mono_disp_t *)drv->user_data;
    const uint8_t *frame = (const uint8_t *)color_map;
    const lv_coord_t width = ctx->hres;

    LV_UNUSED(area);

    for (lv_coord_t page = 0; page < ctx->vres / PAGE_HEIGHT; ++page) {
        const uint8_t *src = frame + (page * width);
        uint8_t *shadow = ctx->shadow + (page * width);

        lv_coord_t first = 0;
        lv_coord_t last = width - 1;

        if (ctx->shadow_valid) {
            while (first <= last && src[first] == shadow[first]) {
                first++;
            }
            if (first > last) {
                continue;
            }
            while (src[last] == shadow[last]) {
                last--;
            }
        }

        // draw_bitmap() end coordinates are exclusive
        esp_lcd_panel_draw_bitmap(ctx->panel, first, page * PAGE_HEIGHT,
                                  last + 1, (page * PAGE_HEIGHT) + PAGE_HEIGHT,
                                  src + first);
        memcpy(shadow + first, src + first, (size_t)(last - first + 1));
    }

    ctx->shadow_valid = true;
    lv_disp_flush_ready(drv);
}

lv_disp_t *mono_disp_add(esp_lcd_panel_handle_t panel, lv_coord_t hres, lv_coord_t vres) {
    if (panel == NULL || (vres % PAGE_HEIGHT) != 0) {
        ESP_LOGE(TAG, "Bad panel handle or vres not a multiple of %d!", PAGE_HEIGHT);
        return NULL;
    }

    mono_disp_t *ctx = calloc(1, sizeof(mono_disp_t));
    if (ctx == NULL) {
        ESP_LOGE(TAG, "Unable to allocate display context!");
        return NULL;
    }
    ctx->panel = panel;
    ctx->hres = hres;
    ctx->vres = vres;

    // set_px_cb only fills the first hres*vres/8 bytes, but LVGL sizes its row
    // math off the full pixel count, so the buffer has to be that large
    lv_color_t *buf = heap_caps_malloc(hres * vres * sizeof(lv_color_t), MALLOC_CAP_DMA);
    ctx->shadow = calloc(1, (hres * vres) / PAGE_HEIGHT);
    if (buf == NULL || ctx->shadow == NULL) {
        ESP_LOGE(TAG, "Unable to allocate display buffers!");
        free(buf);
        free(ctx->shadow);
        free(ctx);
        return NULL;
    }

    lv_disp_draw_buf_init(&ctx->draw_buf, buf, NULL, hres * vres);

    lv_disp_drv_init(&ctx->drv);
    ctx->drv.hor_res = hres;
    ctx->drv.ver_res = vres;
    ctx->drv.draw_buf = &ctx->draw_buf;
    ctx->drv.flush_cb = mono_disp_flush;
    ctx->drv.set_px_cb = mono_disp_set_px;
    ctx->drv.full_refresh = 1;
    ctx->drv.user_data = ctx;

    return lv_disp_drv_register(&ctx->drv);
}
