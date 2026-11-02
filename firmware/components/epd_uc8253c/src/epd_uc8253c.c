#include "epd_uc8253c.h"
#include "esp_timer.h"
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "epd_uc8253c";

// UC8253 Commands
#define CMD_PANEL_SETTING       0x00
#define CMD_POWER_SETTING       0x01
#define CMD_POWER_OFF           0x02
#define CMD_POWER_ON            0x04
#define CMD_BOOSTER_SOFT_START  0x06
#define CMD_DEEP_SLEEP          0x07
#define CMD_DATA_START_TRANS_1  0x10 // DTM1 (Old Image Plane)
#define CMD_DATA_STOP_TRANS     0x11
#define CMD_DISPLAY_REFRESH     0x12 // DRF (Master Activation)
#define CMD_DATA_START_TRANS_2  0x13 // DTM2 (New Image Plane)
#define CMD_VCOM_DATA_INTERVAL  0x50 // CDI
#define CMD_RESOLUTION_SETTING  0x61 // TRES

static epd_config_t s_config;
static spi_device_handle_t s_spi_dev = NULL;
static uint8_t *s_buffer_curr = NULL;
static uint8_t *s_buffer_prev = NULL;
static uint32_t s_partial_count = 0;
static bool s_is_initialized = false;

static inline void set_dc(int level)
{
    gpio_set_level(s_config.pin_dc, level);
}

static inline void set_cs(int level)
{
    gpio_set_level(s_config.pin_cs, level);
}

static inline void set_res(int level)
{
    gpio_set_level(s_config.pin_res, level);
}

bool epd_is_busy(void)
{
    // BUSY pin on WeAct / GoodDisplay UC8253 is Active-LOW:
    // 0 = Busy, 1 = Idle/Ready
    return (gpio_get_level(s_config.pin_busy) == 0);
}

static esp_err_t wait_until_idle(uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    while (epd_is_busy()) {
        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed += 10;
        if (elapsed >= timeout_ms) {
            ESP_LOGW(TAG, "Timeout waiting for E-Paper BUSY line (%lu ms)", timeout_ms);
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

static esp_err_t write_cmd(uint8_t cmd)
{
    set_dc(0); // 0 = Command
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    return spi_device_polling_transmit(s_spi_dev, &t);
}

static esp_err_t write_data(const uint8_t *data, size_t len)
{
    if (len == 0 || data == NULL) return ESP_OK;
    set_dc(1); // 1 = Data

    // Transmit in chunks if length exceeds DMA limits
    size_t chunk_size = 4096;
    size_t offset = 0;

    while (offset < len) {
        size_t current_chunk = (len - offset > chunk_size) ? chunk_size : (len - offset);
        spi_transaction_t t = {
            .length = current_chunk * 8,
            .tx_buffer = data + offset,
        };
        esp_err_t ret = spi_device_polling_transmit(s_spi_dev, &t);
        if (ret != ESP_OK) return ret;
        offset += current_chunk;
    }
    return ESP_OK;
}

static void hardware_reset(void)
{
    set_res(1);
    vTaskDelay(pdMS_TO_TICKS(10));
    set_res(0);
    vTaskDelay(pdMS_TO_TICKS(15));
    set_res(1);
    vTaskDelay(pdMS_TO_TICKS(15));
    wait_until_idle(2000);
}

esp_err_t epd_init(const epd_config_t *config)
{
    if (config == NULL) return ESP_ERR_INVALID_ARG;
    memcpy(&s_config, config, sizeof(epd_config_t));

    ESP_LOGI(TAG, "Initializing WeAct 3.7\" E-Paper (UC8253C)...");

    // 1. Allocate DMA-capable framebuffers
    if (s_buffer_curr == NULL) {
        s_buffer_curr = (uint8_t *)heap_caps_malloc(EPD_BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }
    if (s_buffer_prev == NULL) {
        s_buffer_prev = (uint8_t *)heap_caps_malloc(EPD_BUFFER_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }

    if (!s_buffer_curr || !s_buffer_prev) {
        ESP_LOGE(TAG, "Failed to allocate EPD DMA framebuffers (%d bytes each)", EPD_BUFFER_SIZE);
        return ESP_ERR_NO_MEM;
    }

    // Default: white screen (0xFF in monochrome EPD = white pixel)
    memset(s_buffer_curr, 0xFF, EPD_BUFFER_SIZE);
    memset(s_buffer_prev, 0xFF, EPD_BUFFER_SIZE);

    // 2. Configure GPIOs
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_config.pin_dc) | (1ULL << s_config.pin_res),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    gpio_config_t busy_conf = {
        .pin_bit_mask = (1ULL << s_config.pin_busy),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE, // Internal pullup
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&busy_conf);

    // 3. Initialize SPI Bus with DMA
    spi_bus_config_t buscfg = {
        .mosi_io_num = s_config.pin_sda,
        .miso_io_num = -1,
        .sclk_io_num = s_config.pin_scl,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096 * 8,
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "SPI bus initialize failed: %s", esp_err_to_name(ret));
        return ret;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 10 * 1000 * 1000, // 10 MHz
        .mode = 0,                          // CPOL=0, CPHA=0
        .spics_io_num = s_config.pin_cs,
        .queue_size = 7,
    };

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus add device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 4. Hardware reset & verify panel communication
    hardware_reset();

    s_partial_count = 0;
    s_is_initialized = true;

    ESP_LOGI(TAG, "EPD UC8253C initialized successfully. Resolution: %dx%d (Landscape)", EPD_WIDTH, EPD_HEIGHT);
    return ESP_OK;
}

void epd_clear_buffer(epd_color_t color)
{
    if (!s_buffer_curr) return;
    uint8_t fill = (color == EPD_COLOR_WHITE) ? 0xFF : 0x00;
    memset(s_buffer_curr, fill, EPD_BUFFER_SIZE);
}

void epd_draw_pixel(int16_t x, int16_t y, epd_color_t color)
{
    if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT) return;

    // Coordinate mapping: Logical Landscape (416 x 240) -> Physical Portrait (240 x 416)
    // Physical X: 0..239, Physical Y: 0..415
    int16_t xp = y;
    int16_t yp = (EPD_PHYSICAL_HEIGHT - 1) - x;

    uint32_t byte_idx = (yp * (EPD_PHYSICAL_WIDTH / 8)) + (xp / 8);
    uint8_t bit_mask = (1 << (7 - (xp % 8)));

    if (color == EPD_COLOR_WHITE) {
        s_buffer_curr[byte_idx] |= bit_mask;  // 1 = White
    } else {
        s_buffer_curr[byte_idx] &= ~bit_mask; // 0 = Black
    }
}

uint8_t *epd_get_framebuffer(void)
{
    return s_buffer_curr;
}

static esp_err_t send_panel_init_sequence(void)
{
    // Booster soft start
    const uint8_t btst[] = {0x17, 0x17, 0x17};
    write_cmd(CMD_BOOSTER_SOFT_START);
    write_data(btst, sizeof(btst));

    // Power On
    write_cmd(CMD_POWER_ON);
    wait_until_idle(1000);

    // Panel setting (LUT from OTP, KW-3f, KWR-2f)
    const uint8_t psr[] = {0x1F};
    write_cmd(CMD_PANEL_SETTING);
    write_data(psr, sizeof(psr));

    // Resolution: 240 x 416
    const uint8_t tres[] = {0x00, 0xF0, 0x01, 0xA0};
    write_cmd(CMD_RESOLUTION_SETTING);
    write_data(tres, sizeof(tres));

    // VCOM and Data Interval Setting (CDI - border white)
    const uint8_t cdi[] = {0x97};
    write_cmd(CMD_VCOM_DATA_INTERVAL);
    write_data(cdi, sizeof(cdi));

    return ESP_OK;
}

esp_err_t epd_update(epd_refresh_mode_t mode)
{
    if (!s_is_initialized) return ESP_ERR_INVALID_STATE;

    // Ghosting prevention: auto-escalate to full refresh every N partial updates
    if (mode == EPD_REFRESH_FAST_PARTIAL && s_partial_count >= EPD_FULL_REFRESH_INTERVAL) {
        ESP_LOGI(TAG, "Partial refresh count reached %lu. Auto-triggering Full Refresh to prevent ghosting.", (unsigned long)s_partial_count);
        mode = EPD_REFRESH_FULL;
    }

    int64_t t_start = esp_timer_get_time();

    if (mode == EPD_REFRESH_FULL) {
        ESP_LOGD(TAG, "Starting Full Refresh cycle (~2.5s)...");
        hardware_reset();
        send_panel_init_sequence();

        // Write DTM1 (Old) and DTM2 (New)
        write_cmd(CMD_DATA_START_TRANS_1);
        write_data(s_buffer_curr, EPD_BUFFER_SIZE);

        write_cmd(CMD_DATA_START_TRANS_2);
        write_data(s_buffer_curr, EPD_BUFFER_SIZE);

        // Trigger Display Refresh
        write_cmd(CMD_DISPLAY_REFRESH);
        wait_until_idle(5000);

        s_partial_count = 0;
    } else {
        ESP_LOGD(TAG, "Starting Fast Partial Refresh cycle (~0.3s)...");
        // Fast differential update: DTM1 has previous frame, DTM2 has new frame
        send_panel_init_sequence();

        write_cmd(CMD_DATA_START_TRANS_1);
        write_data(s_buffer_prev, EPD_BUFFER_SIZE);

        write_cmd(CMD_DATA_START_TRANS_2);
        write_data(s_buffer_curr, EPD_BUFFER_SIZE);

        write_cmd(CMD_DISPLAY_REFRESH);
        wait_until_idle(2000);

        s_partial_count++;
    }

    // Safety: Power off high-voltage charge pump to protect the electrophoretic display
    write_cmd(CMD_POWER_OFF);
    wait_until_idle(500);

    // Synchronize previous buffer
    memcpy(s_buffer_prev, s_buffer_curr, EPD_BUFFER_SIZE);

    int64_t elapsed_ms = (esp_timer_get_time() - t_start) / 1000;
    ESP_LOGI(TAG, "EPD Update finished in %lld ms (Mode: %s, Partial Count: %lu)",
             elapsed_ms, (mode == EPD_REFRESH_FULL) ? "FULL" : "PARTIAL", (unsigned long)s_partial_count);

    return ESP_OK;
}

esp_err_t epd_deep_sleep(void)
{
    if (!s_is_initialized) return ESP_ERR_INVALID_STATE;

    ESP_LOGI(TAG, "Entering EPD Deep Sleep (<0.01 uA static current)...");
    write_cmd(CMD_POWER_OFF);
    wait_until_idle(500);

    const uint8_t dslp[] = {0xA5};
    write_cmd(CMD_DEEP_SLEEP);
    write_data(dslp, sizeof(dslp));

    return ESP_OK;
}

uint32_t epd_get_partial_count(void)
{
    return s_partial_count;
}
