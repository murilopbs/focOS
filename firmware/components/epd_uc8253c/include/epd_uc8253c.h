#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Physical display panel geometry
#define EPD_PHYSICAL_WIDTH   240
#define EPD_PHYSICAL_HEIGHT  416

// Logical landscape geometry (desk view orientation)
#define EPD_WIDTH            416
#define EPD_HEIGHT           240
#define EPD_BUFFER_SIZE      ((EPD_PHYSICAL_WIDTH * EPD_PHYSICAL_HEIGHT) / 8) // 12,480 bytes

// Automatic Full Refresh threshold to prevent ghosting
#define EPD_FULL_REFRESH_INTERVAL 10

typedef enum {
    EPD_COLOR_BLACK = 0,
    EPD_COLOR_WHITE = 1
} epd_color_t;

typedef enum {
    EPD_REFRESH_FAST_PARTIAL = 0,
    EPD_REFRESH_FULL         = 1
} epd_refresh_mode_t;

typedef struct {
    int pin_mosi;
    int pin_sclk;
    int pin_cs;
    int pin_dc;
    int pin_res;
    int pin_busy;
} epd_config_t;

/**
 * @brief Initialize SPI bus with DMA and configure E-Paper control GPIOs
 */
esp_err_t epd_init(const epd_config_t *config);

/**
 * @brief Clear the drawing buffer to black or white
 */
void epd_clear_buffer(epd_color_t color);

/**
 * @brief Set pixel in landscape coordinates (x: 0..415, y: 0..239)
 */
void epd_draw_pixel(int16_t x, int16_t y, epd_color_t color);

/**
 * @brief Get pointer to the internal drawing buffer (used by LVGL flush)
 */
uint8_t *epd_get_framebuffer(void);

/**
 * @brief Push buffer to display and trigger update (Full or Fast Partial)
 *        Sends POWER_OFF (0x02) after update to protect the charge pump
 */
esp_err_t epd_update(epd_refresh_mode_t mode);

/**
 * @brief Put display into deep sleep (< 0.01 uA static current)
 */
esp_err_t epd_deep_sleep(void);

/**
 * @brief Get total consecutive partial refreshes executed
 */
uint32_t epd_get_partial_count(void);

/**
 * @brief Check if display is currently busy (refreshing)
 */
bool epd_is_busy(void);

#ifdef __cplusplus
}
#endif
