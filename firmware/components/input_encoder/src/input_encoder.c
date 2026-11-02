#include "input_encoder.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "input_encoder";

// Event Base Definition
ESP_EVENT_DEFINE_BASE(FOCOS_INPUT_EVENT);

#define PCNT_HIGH_LIMIT 1000
#define PCNT_LOW_LIMIT  -1000
#define ENCODER_STEPS_PER_DETENT 2 // Standard KY-040 detent pulse threshold
#define GPIO_BOOT_BUTTON 0         // Onboard ESP32 BOOT button

typedef enum {
    BTN_STATE_IDLE = 0,
    BTN_STATE_PRESSED,
    BTN_STATE_WAIT_DOUBLE,
    BTN_STATE_WAIT_RELEASE,
} btn_state_t;

static pcnt_unit_handle_t s_pcnt_unit = NULL;
static int32_t s_last_count = 0;
static input_encoder_config_t s_cfg;

static void serial_input_task(void *pvParameters)
{
    // Install UART driver on UART0 if not already active
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_param_config(UART_NUM_0, &uart_config);
    uart_driver_install(UART_NUM_0, 256, 0, 0, NULL, 0);

    ESP_LOGI(TAG, "--------------------------------------------------------");
    ESP_LOGI(TAG, ">>> CONTROLE VIA TECLADO NO MONITOR SERIAL ATIVADO! <<<");
    ESP_LOGI(TAG, " [W] ou [K] (ou Seta p/ Cima)  : Subir cursor (Anterior)");
    ESP_LOGI(TAG, " [S] ou [J] (ou Seta p/ Baixo) : Descer cursor (Proximo)");
    ESP_LOGI(TAG, " [Espaco] / [Enter] / [C]      : Marcar/Desmarcar tarefa");
    ESP_LOGI(TAG, " [D]                           : Duplo clique");
    ESP_LOGI(TAG, " [H]                           : Ativar Modo Hiperfoco");
    ESP_LOGI(TAG, " Dica: O botao BOOT da placa (GPIO 0) tambem funciona!");
    ESP_LOGI(TAG, "--------------------------------------------------------");

    uint8_t ch;
    bool in_escape = false;
    bool in_bracket = false;

    while (1) {
        int len = uart_read_bytes(UART_NUM_0, &ch, 1, pdMS_TO_TICKS(50));
        if (len > 0) {
            // ANSI Arrow Keys parser (\x1b[A / \x1b[B)
            if (ch == 0x1B) {
                in_escape = true;
                in_bracket = false;
                continue;
            }
            if (in_escape && ch == '[') {
                in_bracket = true;
                continue;
            }
            if (in_escape && in_bracket) {
                in_escape = false;
                in_bracket = false;
                if (ch == 'A') { // Up Arrow
                    ESP_LOGI(TAG, "Serial: Seta Cima -> PREV");
                    esp_event_post(FOCOS_INPUT_EVENT, FOCOS_INPUT_ENC_PREV, NULL, 0, pdMS_TO_TICKS(10));
                    continue;
                } else if (ch == 'B') { // Down Arrow
                    ESP_LOGI(TAG, "Serial: Seta Baixo -> NEXT");
                    esp_event_post(FOCOS_INPUT_EVENT, FOCOS_INPUT_ENC_NEXT, NULL, 0, pdMS_TO_TICKS(10));
                    continue;
                }
            }
            in_escape = false;
            in_bracket = false;

            // Direct keyboard keys
            if (ch == 'w' || ch == 'W' || ch == 'k' || ch == 'K') {
                ESP_LOGI(TAG, "Serial: '%c' -> PREV (Cursor p/ Cima)", ch);
                esp_event_post(FOCOS_INPUT_EVENT, FOCOS_INPUT_ENC_PREV, NULL, 0, pdMS_TO_TICKS(10));
            } else if (ch == 's' || ch == 'S' || ch == 'j' || ch == 'J') {
                ESP_LOGI(TAG, "Serial: '%c' -> NEXT (Cursor p/ Baixo)", ch);
                esp_event_post(FOCOS_INPUT_EVENT, FOCOS_INPUT_ENC_NEXT, NULL, 0, pdMS_TO_TICKS(10));
            } else if (ch == ' ' || ch == '\r' || ch == '\n' || ch == 'c' || ch == 'C') {
                ESP_LOGI(TAG, "Serial: [CLICK] -> Concluir/Alternar Tarefa");
                esp_event_post(FOCOS_INPUT_EVENT, FOCOS_INPUT_CLICK_SINGLE, NULL, 0, pdMS_TO_TICKS(10));
            } else if (ch == 'd' || ch == 'D') {
                ESP_LOGI(TAG, "Serial: [DOUBLE CLICK] -> Alternar Modo");
                esp_event_post(FOCOS_INPUT_EVENT, FOCOS_INPUT_CLICK_DOUBLE, NULL, 0, pdMS_TO_TICKS(10));
            } else if (ch == 'h' || ch == 'H') {
                ESP_LOGI(TAG, "Serial: [LONG CLICK] -> Modo Hiperfoco");
                esp_event_post(FOCOS_INPUT_EVENT, FOCOS_INPUT_CLICK_LONG, NULL, 0, pdMS_TO_TICKS(10));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
}

static void encoder_task(void *pvParameters)
{
    btn_state_t btn_state = BTN_STATE_IDLE;
    int64_t t_press = 0;
    int64_t t_release = 0;

    while (1) {
        // --- 1. Process Rotary Encoder Hardware PCNT (if knob is plugged in) ---
        if (s_pcnt_unit) {
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
        }

        // --- 2. Process Button FSM (supports external SW OR onboard BOOT button) ---
        // Both are Active-LOW: 0 = Pressed, 1 = Released
        int level_ext = gpio_get_level(s_cfg.pin_sw);
        int level_boot = gpio_get_level(GPIO_BOOT_BUTTON);
        int btn_level = (level_ext == 0 || level_boot == 0) ? 0 : 1;

        int64_t now = esp_timer_get_time() / 1000; // Milliseconds

        switch (btn_state) {
            case BTN_STATE_IDLE:
                if (btn_level == 0) {
                    t_press = now;
                    btn_state = BTN_STATE_PRESSED;
                }
                break;

            case BTN_STATE_PRESSED:
                if (btn_level == 0) {
                    if (now - t_press >= 1200) {
                        ESP_LOGI(TAG, "Hardware Button: LONG PRESS");
                        esp_event_post(FOCOS_INPUT_EVENT, FOCOS_INPUT_CLICK_LONG, NULL, 0, pdMS_TO_TICKS(10));
                        btn_state = BTN_STATE_WAIT_RELEASE;
                    }
                } else {
                    int64_t press_duration = now - t_press;
                    if (press_duration >= 20 && press_duration < 400) {
                        t_release = now;
                        btn_state = BTN_STATE_WAIT_DOUBLE;
                    } else {
                        btn_state = BTN_STATE_IDLE;
                    }
                }
                break;

            case BTN_STATE_WAIT_DOUBLE:
                if (btn_level == 0) {
                    ESP_LOGI(TAG, "Hardware Button: DOUBLE CLICK");
                    esp_event_post(FOCOS_INPUT_EVENT, FOCOS_INPUT_CLICK_DOUBLE, NULL, 0, pdMS_TO_TICKS(10));
                    btn_state = BTN_STATE_WAIT_RELEASE;
                } else if (now - t_release > 350) {
                    ESP_LOGI(TAG, "Hardware Button: SINGLE CLICK");
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

    ESP_LOGI(TAG, "Initializing Input Subsystem (Encoder PCNT + Serial CLI + Onboard BOOT button)...");

    // 1. Configure Hardware Pulse Counter Unit
    pcnt_unit_config_t unit_config = {
        .high_limit = PCNT_HIGH_LIMIT,
        .low_limit = PCNT_LOW_LIMIT,
    };
    esp_err_t err = pcnt_new_unit(&unit_config, &s_pcnt_unit);
    if (err == ESP_OK) {
        pcnt_glitch_filter_config_t filter_config = {
            .max_glitch_ns = 1000,
        };
        pcnt_unit_set_glitch_filter(s_pcnt_unit, &filter_config);

        pcnt_chan_config_t chan_a_config = {
            .edge_gpio_num = s_cfg.pin_clk,
            .level_gpio_num = s_cfg.pin_dt,
        };
        pcnt_channel_handle_t pcnt_chan_a = NULL;
        pcnt_new_channel(s_pcnt_unit, &chan_a_config, &pcnt_chan_a);
        pcnt_channel_set_edge_action(pcnt_chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
        pcnt_channel_set_level_action(pcnt_chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

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
    }

    // 2. Configure SW Pin and Onboard BOOT Button Pin (GPIO 0)
    gpio_config_t btn_conf = {
        .pin_bit_mask = (1ULL << s_cfg.pin_sw) | (1ULL << GPIO_BOOT_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_conf);

    // 3. Start Button and Hardware Tasks
    xTaskCreate(encoder_task, "input_btn_task", 3072, NULL, 5, NULL);

    // 4. Start Serial CLI Terminal Input Task
    xTaskCreate(serial_input_task, "serial_cli_task", 3072, NULL, 5, NULL);

    ESP_LOGI(TAG, "Input Subsystem ready.");
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
