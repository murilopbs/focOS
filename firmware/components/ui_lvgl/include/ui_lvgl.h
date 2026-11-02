#pragma once

#include <stdbool.h>
#include "esp_err.h"
#include "lvgl.h"
#include "epd_uc8253c.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize LVGL, allocate drawing buffers, register display driver and start UI task
 */
esp_err_t ui_lvgl_init(void);

/**
 * @brief Thread-safe lock for LVGL API calls from other tasks
 */
bool ui_lock(uint32_t timeout_ms);

/**
 * @brief Release the thread-safe UI lock
 */
void ui_unlock(void);

/**
 * @brief Trigger physical E-Paper refresh of the current LVGL framebuffer
 * @param mode Fast partial (~0.3s) or full (~2.5s)
 */
esp_err_t ui_request_refresh(epd_refresh_mode_t mode);

/**
 * @brief Check if UI has pending dirty regions
 */
bool ui_has_pending_update(void);

#ifdef __cplusplus
}
#endif

