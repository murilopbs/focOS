#include "input_encoder.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "input_encoder";

// Event Base Definition
ESP_EVENT_DEFINE_BASE(FOCOS_INPUT_EVENT);

#define PCNT_HIGH_LIMIT 1000
#define PCNT_LOW_LIMIT  -1000
#define ENCODER_STEPS_PER_DETENT 2 // Standard KY-040 detent pulse threshold

typedef enum {
    BTN_STATE_IDLE = 0,
    BTN_STATE_PRESSED,
    BTN_STATE_WAIT_DOUBLE,
    BTN_STATE_WAIT_RELEASE,
} btn_state_t;

static pcnt_unit_handle_t s_pcnt_unit = NULL;
static int32_t s_last_count = 0;
static input_encoder_config_t s_cfg;

static void encoder_task(void *pvParameters)
{
    btn_state_t btn_state = BTN_STATE_IDLE;
    int64_t t_press = 0;
    int64_t t_release = 0;

    while (1) {
        // --- 1. Process Rotary Encoder Hardware PCNT ---
        int cur_count = 0;
        if (pcnt_unit_get_count(s_pcnt_unit, &cur_count) == ESP_OK) {
            int delta = cur_count - s_last_count;
            if (delta >= ENCODER_STEPS_PER_DETENT) {
                int steps = delta / ENCODER_STEPS_PER_DETENT;
                s_last_count += steps * ENCODER_STEPS_PER_DETENT;
                for (int i = 0; i < steps; i++) {
                    esp_event_post(FOCOS_INPUT_EVENT, FOCOS_INPUT_ENC_NEXT, NULL, 0, pdMS_TO_TICKS(10));
                }
            } else if (delta <= -ENCODER_STEPS_PER_DETENT) {
                int steps = (-delta) / ENCODER_STEPS_PER_DETENT;
                s_last_count -= steps * ENCODER_STEPS_PER_DETENT;
                for (int i = 0; i < steps; i++) {
                    esp_event_post(FOCOS_INPUT_EVENT, FOCOS_INPUT_ENC_PREV, NULL, 0, pdMS_TO_TICKS(10));
                }
            }
        }

        // --- 2. Process Button Finite State Machine (Debounced & Multi-click) ---
        int btn_level = gpio_get_level(s_cfg.pin_sw); // 0 = Pressed, 1 = Released
        int64_t now = esp_timer_get_time() / 1000;    // Milliseconds

        switch (btn_state) {
            case BTN_STATE_IDLE:
                if (btn_level == 0) {
                    t_press = now;
                    btn_state = BTN_STATE_PRESSED;
                }
                break;

            case BTN_STATE_PRESSED:
                if (btn_level == 0) {
                    // Check for Long Press (held >= 1200ms)
                    if (now - t_press >= 1200) {
                        ESP_LOGD(TAG, "Button: LONG PRESS detected");
                        esp_event_post(FOCOS_INPUT_EVENT, FOCOS_INPUT_CLICK_LONG, NULL, 0, pdMS_TO_TICKS(10));
                        btn_state = BTN_STATE_WAIT_RELEASE;
                    }
                } else {
                    // Released before long press
                    int64_t press_duration = now - t_press;
                    if (press_duration >= 20 && press_duration < 400) { // Valid debounce range
                        t_release = now;
                        btn_state = BTN_STATE_WAIT_DOUBLE;
                    } else {
                        btn_state = BTN_STATE_IDLE;
                    }
                }
                break;

            case BTN_STATE_WAIT_DOUBLE:
                if (btn_level == 0) {
                    // Second press detected within 350ms window -> Double Click!
                    ESP_LOGD(TAG, "Button: DOUBLE CLICK detected");
                    esp_event_post(FOCOS_INPUT_EVENT, FOCOS_INPUT_CLICK_DOUBLE, NULL, 0, pdMS_TO_TICKS(10));
                    btn_state = BTN_STATE_WAIT_RELEASE;
                } else if (now - t_release > 350) {
                    // Timeout expired without second click -> Single Click!
                    ESP_LOGD(TAG, "Button: SINGLE CLICK detected");
                    esp_event_post(FOCOS_INPUT_EVENT, FOCOS_INPUT_CLICK_SINGLE, NULL, 0, pdMS_TO_TICKS(10));
                    btn_state = BTN_STATE_IDLE;
                }
                break;

            case BTN_STATE_WAIT_RELEASE:
                if (btn_level == 1) {
                    btn_state = BTN_STATE_IDLE;
                }
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

esp_err_t input_encoder_init(const input_encoder_config_t *config)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    memcpy(&s_cfg, config, sizeof(input_encoder_config_t));

    ESP_LOGI(TAG, "Initializing Rotary Encoder (CLK=%d, DT=%d, SW=%d) via hardware PCNT...",
             s_cfg.pin_clk, s_cfg.pin_dt, s_cfg.pin_sw);

    // 1. Configure Hardware Pulse Counter Unit
    pcnt_unit_config_t unit_config = {
        .high_limit = PCNT_HIGH_LIMIT,
        .low_limit = PCNT_LOW_LIMIT,
    };
    esp_err_t err = pcnt_new_unit(&unit_config, &s_pcnt_unit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create PCNT unit: %s", esp_err_to_name(err));
        return err;
    }

    // Hardware Glitch Filter (1000 ns debounce in silicon)
    pcnt_glitch_filter_config_t filter_config = {
        .max_glitch_ns = 1000,
    };
    pcnt_unit_set_glitch_filter(s_pcnt_unit, &filter_config);

    // Channel A: Edge CLK, Level DT
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num = s_cfg.pin_clk,
        .level_gpio_num = s_cfg.pin_dt,
    };
    pcnt_channel_handle_t pcnt_chan_a = NULL;
    pcnt_new_channel(s_pcnt_unit, &chan_a_config, &pcnt_chan_a);
    pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    // Channel B: Edge DT, Level CLK
    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num = s_cfg.pin_dt,
        .level_gpio_num = s_cfg.pin_clk,
    };
    pcnt_channel_handle_t pcnt_chan_b = NULL;
    pcnt_new_channel(s_pcnt_unit, &chan_b_config, &pcnt_chan_b);
    pcnt_channel_set_edge_action(pcnt_chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(pcnt_chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    pcnt_unit_enable(s_pcnt_unit);
    pcnt_unit_clear_count(s_pcnt_unit);
    pcnt_unit_start(s_pcnt_unit);
    s_last_count = 0;

    // 2. Configure SW Push Button GPIO with Pull-up
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << s_cfg.pin_sw),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_conf);

    // 3. Start FreeRTOS Input Task
    BaseType_t res = xTaskCreate(
        encoder_task,
        "input_task",
        3072,
        NULL,
        5,
        NULL
    );

    if (res != pdPASS) {
        ESP_LOGE(TAG, "Failed to start input_task");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Input Encoder subsystem ready.");
    return ESP_OK;
}

int32_t input_encoder_get_position(void)
{
    int count = 0;
    if (s_pcnt_unit) {
        pcnt_unit_get_count(s_pcnt_unit, &count);
    }
    return count / ENCODER_STEPS_PER_DETENT;
}
