#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

// focOS Input Event Base
ESP_EVENT_DECLARE_BASE(FOCOS_INPUT_EVENT);

typedef enum {
    FOCOS_INPUT_ENC_NEXT = 0,   // Knob turned clockwise (cursor down/next)
    FOCOS_INPUT_ENC_PREV,       // Knob turned counter-clockwise (cursor up/prev)
    FOCOS_INPUT_CLICK_SINGLE,   // Single short press (< 400ms)
    FOCOS_INPUT_CLICK_DOUBLE,   // Double click (2 presses in < 350ms)
    FOCOS_INPUT_CLICK_LONG,     // Long press (held >= 1200ms)
} focos_input_event_t;

typedef struct {
    int pin_clk;  // Channel A (CLK)
    int pin_dt;   // Channel B (DT)
    int pin_sw;   // Push button (SW)
} input_encoder_config_t;

/**
 * @brief Initialize the rotary encoder hardware PCNT and button task
 */
esp_err_t input_encoder_init(const input_encoder_config_t *config);

/**
 * @brief Get the current cumulative position count of the encoder
 */
int32_t input_encoder_get_position(void);

#ifdef __cplusplus
}
#endif
