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
#include "input_encoder.h"

static const char *TAG = "focOS_kernel";

#define NUM_TASKS 3

typedef struct {
    char title[64];
    bool completed;
} sample_task_t;

static sample_task_t s_tasks[NUM_TASKS] = {
    {"Conectar display WeAct 3.7 SPI", true},
    {"Girar knob para testar PCNT", false},
    {"Dar 1 clique curto para concluir", false}
};

static int s_selected_idx = 1; // Cursor starts on item 1
static lv_obj_t *s_task_boxes[NUM_TASKS];
static lv_obj_t *s_task_labels[NUM_TASKS];
static lv_obj_t *s_lbl_progress = NULL;
static lv_obj_t *s_bar_progress = NULL;

static void update_task_ui_selection(void)
{
    if (!ui_lock(500)) return;

    int completed_count = 0;
    for (int i = 0; i < NUM_TASKS; i++) {
        if (s_tasks[i].completed) completed_count++;

        // Update border thickness based on cursor
        lv_obj_set_style_border_width(s_task_boxes[i], (i == s_selected_idx) ? 2 : 1, 0);

        // Update checkbox text
        char buf[80];
        snprintf(buf, sizeof(buf), "[%c] %s", s_tasks[i].completed ? 'X' : ' ', s_tasks[i].title);
        lv_label_set_text(s_task_labels[i], buf);
    }

    // Update progress bar & label
    int percent = (completed_count * 100) / NUM_TASKS;
    char prog_str[32];
    snprintf(prog_str, sizeof(prog_str), "%d/%d concluidas (%d%%)", completed_count, NUM_TASKS, percent);
    if (s_lbl_progress) lv_label_set_text(s_lbl_progress, prog_str);
    if (s_bar_progress) lv_bar_set_value(s_bar_progress, percent, LV_ANIM_OFF);

    ui_unlock();

    // Trigger fast partial refresh of the E-Paper (~0.3s)
    ui_request_refresh(EPD_REFRESH_FAST_PARTIAL);
}

static void on_input_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
        case FOCOS_INPUT_ENC_NEXT:
            ESP_LOGI(TAG, "Input: Knob NEXT (Clockwise)");
            if (s_selected_idx < NUM_TASKS - 1) {
                s_selected_idx++;
                update_task_ui_selection();
            }
            break;

        case FOCOS_INPUT_ENC_PREV:
            ESP_LOGI(TAG, "Input: Knob PREV (Counter-Clockwise)");
            if (s_selected_idx > 0) {
                s_selected_idx--;
                update_task_ui_selection();
            }
            break;

        case FOCOS_INPUT_CLICK_SINGLE:
            ESP_LOGI(TAG, "Input: Button SINGLE CLICK -> Toggle task [%d]", s_selected_idx);
            s_tasks[s_selected_idx].completed = !s_tasks[s_selected_idx].completed;
            update_task_ui_selection();
            break;

        case FOCOS_INPUT_CLICK_DOUBLE:
            ESP_LOGI(TAG, "Input: Button DOUBLE CLICK -> Alternar visualizacao");
            // Reserved for screen mode switching in US-106
            break;

        case FOCOS_INPUT_CLICK_LONG:
            ESP_LOGI(TAG, "Input: Button LONG CLICK -> Ativar Modo Hiperfoco");
            // Reserved for Hiperfoco trigger in US-106
            break;

        default:
            break;
    }
}

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
    lv_label_set_text(lbl_brand, "focOS 1.0  [Knob OK]");
    lv_obj_set_style_text_font(lbl_brand, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_brand, LV_ALIGN_RIGHT_MID, -4, 0);

    // 2. Main Title & Subtitle
    lv_obj_t *lbl_title = lv_label_create(scr);
    lv_label_set_text(lbl_title, "Foco do Dia");
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_18, 0);
    lv_obj_set_pos(lbl_title, 14, 38);

    s_lbl_progress = lv_label_create(scr);
    lv_label_set_text(s_lbl_progress, "1/3 concluidas (33%)");
    lv_obj_set_style_text_font(s_lbl_progress, &lv_font_montserrat_14, 0);
    lv_obj_align(s_lbl_progress, LV_ALIGN_TOP_RIGHT, -14, 40);

    // Progress Bar
    s_bar_progress = lv_bar_create(scr);
    lv_obj_set_size(s_bar_progress, 388, 6);
    lv_obj_set_pos(s_bar_progress, 14, 62);
    lv_bar_set_range(s_bar_progress, 0, 100);
    lv_bar_set_value(s_bar_progress, 33, LV_ANIM_OFF);
    lv_obj_set_style_anim_time(s_bar_progress, 0, 0);
    lv_obj_set_style_bg_color(s_bar_progress, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_bar_progress, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_bar_progress, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bar_progress, lv_color_black(), LV_PART_INDICATOR);

    // 3. Task Items List
    int start_y = 78;
    for (int i = 0; i < NUM_TASKS; i++) {
        s_task_boxes[i] = lv_obj_create(scr);
        lv_obj_set_size(s_task_boxes[i], 388, 38);
        lv_obj_set_pos(s_task_boxes[i], 14, start_y + (i * 44));
        lv_obj_set_style_radius(s_task_boxes[i], 4, 0);
        lv_obj_set_style_border_width(s_task_boxes[i], (i == s_selected_idx) ? 2 : 1, 0);
        lv_obj_set_style_border_color(s_task_boxes[i], lv_color_black(), 0);
        lv_obj_set_style_bg_color(s_task_boxes[i], lv_color_white(), 0);
        lv_obj_set_style_pad_all(s_task_boxes[i], 8, 0);
        lv_obj_clear_flag(s_task_boxes[i], LV_OBJ_FLAG_SCROLLABLE);

        s_task_labels[i] = lv_label_create(s_task_boxes[i]);
        char buf[80];
        snprintf(buf, sizeof(buf), "[%c] %s", s_tasks[i].completed ? 'X' : ' ', s_tasks[i].title);
        lv_label_set_text(s_task_labels[i], buf);
        lv_obj_set_style_text_font(s_task_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_align(s_task_labels[i], LV_ALIGN_LEFT_MID, 4, 0);
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

    // 2. Initialize Default Event Loop
    ESP_LOGI(TAG, "Creating default system event loop...");
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_LOGI(TAG, "System event bus ready.");

    // 3. Register Input Event Handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        FOCOS_INPUT_EVENT,
        ESP_EVENT_ANY_ID,
        on_input_event,
        NULL,
        NULL
    ));

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

    // 6. Initialize Rotary Encoder (Hardware PCNT + Button FSM)
    input_encoder_config_t enc_cfg = {
        .pin_clk = CONFIG_FOCOS_ENC_CLK_GPIO,
        .pin_dt  = CONFIG_FOCOS_ENC_DT_GPIO,
        .pin_sw  = CONFIG_FOCOS_ENC_SW_GPIO,
    };
    ESP_ERROR_CHECK(input_encoder_init(&enc_cfg));

    // 7. Render Initial Screen
    create_initial_ui();

    ESP_LOGI(TAG, "focOS Kernel, UI & Input subsystems running.");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGD(TAG, "Heartbeat | Free Heap: %" PRIu32 " bytes | EPD Partial Count: %lu",
                 (uint32_t)esp_get_free_heap_size(),
                 (unsigned long)epd_get_partial_count());
    }
}
