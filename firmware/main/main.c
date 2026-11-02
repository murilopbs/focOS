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

static void print_pinout_config(void)
{
    ESP_LOGI(TAG, "--- focOS Hardware Pinout (Kconfig) ---");
    ESP_LOGI(TAG, "EPD SPI: MOSI=%d, SCLK=%d, CS=%d, DC=%d, RES=%d, BUSY=%d",
             CONFIG_FOCOS_EPD_MOSI_GPIO,
             CONFIG_FOCOS_EPD_SCLK_GPIO,
             CONFIG_FOCOS_EPD_CS_GPIO,
             CONFIG_FOCOS_EPD_DC_GPIO,
             CONFIG_FOCOS_EPD_RES_GPIO,
             CONFIG_FOCOS_EPD_BUSY_GPIO);
    ESP_LOGI(TAG, "Encoder: CLK=%d, DT=%d, SW=%d",
             CONFIG_FOCOS_ENC_CLK_GPIO,
             CONFIG_FOCOS_ENC_DT_GPIO,
             CONFIG_FOCOS_ENC_SW_GPIO);
    ESP_LOGI(TAG, "Device Name: %s | Timezone: %s",
             CONFIG_FOCOS_DEVICE_NAME,
             CONFIG_FOCOS_TIMEZONE_POSIX);
    ESP_LOGI(TAG, "--------------------------------------");
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
    print_pinout_config();

    ESP_LOGI(TAG, "focOS Kernel bootstrap phase complete. Ready for subsystem initialization.");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGD(TAG, "Heartbeat | Free Heap: %" PRIu32 " bytes", (uint32_t)esp_get_free_heap_size());
    }
}

