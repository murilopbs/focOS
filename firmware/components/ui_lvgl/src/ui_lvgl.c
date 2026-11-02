#include "ui_lvgl.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG = "ui_lvgl";

// 40 horizontal lines drawing buffer (416 * 40 bytes = 16.6 KB)
#define LVGL_DRAW_BUF_LINES 40
#define LVGL_DRAW_BUF_SIZE  (EPD_WIDTH * LVGL_DRAW_BUF_LINES)

static lv_disp_drv_t s_disp_drv;
static lv_disp_draw_buf_t s_disp_draw_buf;
static lv_color_t *s_draw_buf = NULL;
static SemaphoreHandle_t s_ui_mutex = NULL;
static bool s_is_dirty = false;

static void disp_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    for (int32_t y = area->y1; y <= area->y2; y++) {
        for (int32_t x = area->x1; x <= area->x2; x++) {
            // In LVGL 1-bit monochrome: 1 = White, 0 = Black
            epd_color_t c = (color_p->full != 0) ? EPD_COLOR_WHITE : EPD_COLOR_BLACK;
            epd_draw_pixel(x, y, c);
            color_p++;
        }
    }

    s_is_dirty = true;
    lv_disp_flush_ready(drv);
}

static void ui_task(void *pvParameters)
{
    ESP_LOGI(TAG, "UI event loop task started on Core %d", xPortGetCoreID());

    while (1) {
        if (ui_lock(100)) {
            lv_timer_handler();
            ui_unlock();
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t ui_lvgl_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL 8.4 in 1-bit Monochrome mode...");

    s_ui_mutex = xSemaphoreCreateMutex();
    if (!s_ui_mutex) {
        ESP_LOGE(TAG, "Failed to create UI mutex");
        return ESP_ERR_NO_MEM;
    }

    lv_init();

    // Allocate draw buffer in DMA/Internal RAM
    s_draw_buf = (lv_color_t *)heap_caps_malloc(LVGL_DRAW_BUF_SIZE * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_draw_buf) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffer (%d bytes)", (int)(LVGL_DRAW_BUF_SIZE * sizeof(lv_color_t)));
        return ESP_ERR_NO_MEM;
    }

    lv_disp_draw_buf_init(&s_disp_draw_buf, s_draw_buf, NULL, LVGL_DRAW_BUF_SIZE);

    // Register LVGL display driver
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = EPD_WIDTH;
    s_disp_drv.ver_res = EPD_HEIGHT;
    s_disp_drv.flush_cb = disp_flush_cb;
    s_disp_drv.draw_buf = &s_disp_draw_buf;
    s_disp_drv.full_refresh = 0; // Partial render via dirty areas
    lv_disp_drv_register(&s_disp_drv);

    // Apply Monochrome Theme
    lv_theme_t *th = lv_theme_mono_init(lv_disp_get_default(), false, &lv_font_montserrat_14);
    lv_disp_set_theme(lv_disp_get_default(), th);

    // Create FreeRTOS UI Task (pinned to Core 1 to leave Core 0 for WiFi/Crypto)
    BaseType_t res = xTaskCreatePinnedToCore(
        ui_task,
        "focos_ui",
        4096,
        NULL,
        4,
        NULL,
        1
    );

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to create focos_ui task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LVGL Monochrome initialized successfully (Display: %dx%d)", EPD_WIDTH, EPD_HEIGHT);
    return ESP_OK;
}

bool ui_lock(uint32_t timeout_ms)
{
    if (!s_ui_mutex) return false;
    return (xSemaphoreTake(s_ui_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE);
}

void ui_unlock(void)
{
    if (s_ui_mutex) {
        xSemaphoreGive(s_ui_mutex);
    }
}

esp_err_t ui_request_refresh(epd_refresh_mode_t mode)
{
    if (!ui_lock(500)) {
        ESP_LOGW(TAG, "Timeout waiting for UI lock to refresh");
        return ESP_ERR_TIMEOUT;
    }

    // Force pending LVGL render passes
    lv_refr_now(lv_disp_get_default());
    ui_unlock();

    s_is_dirty = false;
    return epd_update(mode);
}

bool ui_has_pending_update(void)
{
    return s_is_dirty;
}

