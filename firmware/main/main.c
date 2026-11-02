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
#include "ui_lvgl.h"

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

static void create_initial_ui(void)
{
    if (!ui_lock(1000)) return;

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_white(), 0);

    // 1. Header Bar
    lv_obj_t *header = lv_obj_create(scr);
    lv_obj_set_size(header, 416, 28);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_style_radius(header, 0, 0);
    lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(header, 2, 0);
    lv_obj_set_style_border_color(header, lv_color_black(), 0);
    lv_obj_set_style_bg_color(header, lv_color_white(), 0);
    lv_obj_set_style_pad_all(header, 4, 0);
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_date = lv_label_create(header);
    lv_label_set_text(lbl_date, "SEG, 02 NOV");
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_date, LV_ALIGN_LEFT_MID, 4, 0);

    lv_obj_t *lbl_brand = lv_label_create(header);
    lv_label_set_text(lbl_brand, "focOS 1.0  [Wi-Fi]");
    lv_obj_set_style_text_font(lbl_brand, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_brand, LV_ALIGN_RIGHT_MID, -4, 0);

    // 2. Main Title & Subtitle
    lv_obj_t *lbl_title = lv_label_create(scr);
    lv_label_set_text(lbl_title, "Foco do Dia");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(lbl_title, 14, 38);

    lv_obj_t *lbl_progress = lv_label_create(scr);
    lv_label_set_text(lbl_progress, "1/3 concluídas (33%)");
    lv_obj_set_style_text_font(lbl_progress, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_progress, LV_ALIGN_TOP_RIGHT, -14, 40);

    // Progress Bar
    lv_obj_t *bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 388, 6);
    lv_obj_set_pos(bar, 14, 62);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 33, LV_ANIM_OFF);
    lv_obj_set_style_anim_time(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_black(), LV_PART_INDICATOR);

    // 3. Task Items List (Demonstration of 1-bit typography & layout)
    const char *tasks[] = {
        "[X] Inicializar WeAct 3.7 E-Paper SPI DMA",
        "[ ] Integrar LVGL 8.4 Monocromatico",
        "[ ] Testar Rotary Encoder (PCNT)"
    };

    int start_y = 78;
    for (int i = 0; i < 3; i++) {
        lv_obj_t *t_box = lv_obj_create(scr);
        lv_obj_set_size(t_box, 388, 38);
        lv_obj_set_pos(t_box, 14, start_y + (i * 44));
        lv_obj_set_style_radius(t_box, 4, 0);
        lv_obj_set_style_border_width(t_box, (i == 1) ? 2 : 1, 0); // Active cursor highlight on item 1
        lv_obj_set_style_border_color(t_box, lv_color_black(), 0);
        lv_obj_set_style_bg_color(t_box, lv_color_white(), 0);
        lv_obj_set_style_pad_all(t_box, 8, 0);
        lv_obj_clear_flag(t_box, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl_task = lv_label_create(t_box);
        lv_label_set_text(lbl_task, tasks[i]);
        lv_obj_set_style_text_font(lbl_task, &lv_font_montserrat_14, 0);
        lv_obj_align(lbl_task, LV_ALIGN_LEFT_MID, 4, 0);
    }

    // 4. Footer info
    lv_obj_t *lbl_footer = lv_label_create(scr);
    lv_label_set_text(lbl_footer, "Gire o knob para navegar | Clique para concluir");
    lv_obj_set_style_text_font(lbl_footer, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_footer, LV_ALIGN_BOTTOM_MID, 0, -4);

    ui_unlock();

    ESP_LOGI(TAG, "Requesting initial Full Refresh of LVGL screen to E-Paper...");
    ui_request_refresh(EPD_REFRESH_FULL);
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

    // 4. Initialize Hardware Driver: WeAct 3.7" E-Paper (UC8253C)
    epd_config_t epd_cfg = {
        .pin_sda  = CONFIG_FOCOS_EPD_SDA_GPIO,
        .pin_scl  = CONFIG_FOCOS_EPD_SCL_GPIO,
        .pin_cs   = CONFIG_FOCOS_EPD_CS_GPIO,
        .pin_dc   = CONFIG_FOCOS_EPD_DC_GPIO,
        .pin_res  = CONFIG_FOCOS_EPD_RES_GPIO,
        .pin_busy = CONFIG_FOCOS_EPD_BUSY_GPIO,
    };
    ESP_ERROR_CHECK(epd_init(&epd_cfg));

    // 5. Initialize Graphics Layer: LVGL 8.4 (1-bit Monochrome)
    ESP_ERROR_CHECK(ui_lvgl_init());

    // 6. Render Initial Screen
    create_initial_ui();

    ESP_LOGI(TAG, "focOS Kernel & UI subsystems running.");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGD(TAG, "Heartbeat | Free Heap: %" PRIu32 " bytes | EPD Partial Count: %lu",
                 (uint32_t)esp_get_free_heap_size(),
                 (unsigned long)epd_get_partial_count());
    }
}
