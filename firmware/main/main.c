#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "epd_uc8253c.h"

static const char *TAG = "focOS_kernel";

static void print_banner(void)
{
    printf("\n");
    printf("  __            ____   _____ \n");
    printf(" / _|          / __ \\ / ____|\n");
    printf("| |_ ___   ___| |  | | (___  \n");
    printf("|  _/ _ \\ / __| |  | |\\___ \\ \n");
    printf("| || (_) | (__| |__| |____) |\n");
    printf("|_| \\___/ \\___|\\____/|_____/ \n");
    printf(">> Ambient E-Paper Desk OS v1.0\n");
    printf(">> Target Hardware: %s\n\n", CONFIG_IDF_TARGET);
}

static void print_system_info(void)
{
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);

    if (esp_flash_get_size(NULL, &flash_size) != ESP_OK) {
        flash_size = 0;
    }

    ESP_LOGI(TAG, "CPU Cores: %d | Silicon Revision: %d", chip_info.cores, chip_info.revision);
    ESP_LOGI(TAG, "Flash Size: %" PRIu32 " MB | Features: %s%s",
             flash_size / (1024 * 1024),
             (chip_info.features & CHIP_FEATURE_WIFI_BGN) ? "802.11bgn " : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "BLE " : "");
    ESP_LOGI(TAG, "Free Internal Heap: %" PRIu32 " KB", (uint32_t)(esp_get_free_internal_heap_size() / 1024));
}

static void init_epaper_display(void)
{
    epd_config_t epd_cfg = {
        .pin_mosi = CONFIG_FOCOS_EPD_MOSI_GPIO,
        .pin_sclk = CONFIG_FOCOS_EPD_SCLK_GPIO,
        .pin_cs   = CONFIG_FOCOS_EPD_CS_GPIO,
        .pin_dc   = CONFIG_FOCOS_EPD_DC_GPIO,
        .pin_res  = CONFIG_FOCOS_EPD_RES_GPIO,
        .pin_busy = CONFIG_FOCOS_EPD_BUSY_GPIO,
    };

    ESP_LOGI(TAG, "Configuring WeAct 3.7\" E-Paper display HAL...");
    esp_err_t err = epd_init(&epd_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize E-Paper display: %s", esp_err_to_name(err));
        return;
    }

    // Clear buffer to white
    epd_clear_buffer(EPD_COLOR_WHITE);

    // Draw initial test geometry (double border in landscape 416 x 240)
    for (int x = 4; x < EPD_WIDTH - 4; x++) {
        epd_draw_pixel(x, 4, EPD_COLOR_BLACK);
        epd_draw_pixel(x, 6, EPD_COLOR_BLACK);
        epd_draw_pixel(x, EPD_HEIGHT - 5, EPD_COLOR_BLACK);
        epd_draw_pixel(x, EPD_HEIGHT - 7, EPD_COLOR_BLACK);
    }
    for (int y = 4; y < EPD_HEIGHT - 4; y++) {
        epd_draw_pixel(4, y, EPD_COLOR_BLACK);
        epd_draw_pixel(6, y, EPD_COLOR_BLACK);
        epd_draw_pixel(EPD_WIDTH - 5, y, EPD_COLOR_BLACK);
        epd_draw_pixel(EPD_WIDTH - 7, y, EPD_COLOR_BLACK);
    }

    // Draw center crosshair and focus card box
    for (int x = 40; x < EPD_WIDTH - 40; x++) {
        epd_draw_pixel(x, 45, EPD_COLOR_BLACK);
    }

    ESP_LOGI(TAG, "Drawing test frame to E-Paper...");
    epd_update(EPD_REFRESH_FULL);
}

void app_main(void)
{
    print_banner();

    // 1. Initialize NVS (Non-Volatile Storage)
    ESP_LOGI(TAG, "Initializing Non-Volatile Storage (NVS)...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing and re-initializing NVS partition...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized successfully.");

    // 2. Initialize the Default System Event Loop
    ESP_LOGI(TAG, "Creating default system event loop...");
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG, "System event bus ready.");

    // 3. Print Hardware & Config Telemetry
    print_system_info();

    // 4. Initialize E-Paper Display
    init_epaper_display();

    ESP_LOGI(TAG, "focOS Kernel bootstrap phase complete. Subsystems running.");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGD(TAG, "Heartbeat | Free Heap: %" PRIu32 " bytes | EPD Partial Count: %lu",
                 (uint32_t)esp_get_free_heap_size(),
                 (unsigned long)epd_get_partial_count());
    }
}
