#include "epd_uc8253c.h"
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_heap_caps.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "epd_uc8253c";

// UC8253 Commands (GoodDisplay GDEY037T03)
#define CMD_PANEL_SETTING       0x00 // PSR
#define CMD_POWER_SETTING       0x01 // PWR
#define CMD_POWER_OFF           0x02 // POF
#define CMD_POWER_ON            0x04 // PON
#define CMD_BOOSTER_SOFT_START  0x06 // BTST
#define CMD_DEEP_SLEEP          0x07 // DSLP
#define CMD_DATA_START_TRANS_1  0x10 // DTM1 (Old image plane)
#define CMD_DISPLAY_REFRESH     0x12 // DRF (Master Activation)
#define CMD_DATA_START_TRANS_2  0x13 // DTM2 (New image plane)
#define CMD_VCOM_DATA_INTERVAL  0x50 // CDI
#define CMD_RESOLUTION_SETTING  0x61 // TRES
#define CMD_PARTIAL_WINDOW      0x90 // PTW
#define CMD_PARTIAL_IN          0x91 // PTI
#define CMD_PARTIAL_OUT         0x92 // PTO
#define CMD_CASCADE_SETTING     0xE0 // CCSET
#define CMD_FORCE_TEMPERATURE   0xE5 // TSSET

static epd_config_t s_config;
static spi_device_handle_t s_spi_dev = NULL;
static uint8_t *s_buffer_curr = NULL;
static uint8_t *s_buffer_prev = NULL;
static uint32_t s_partial_count = 0;
static bool s_is_initialized = false;
static bool s_power_is_on = false;

// DMA-safe static transmission buffer for small commands/data
static uint8_t *s_cmd_buf = NULL;

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
    // BUSY line on WeAct / GoodDisplay UC8253:
    // 0 = Busy, 1 = Idle / Ready
    return (gpio_get_level(s_config.pin_busy) == 0);
}

static esp_err_t wait_until_idle(uint32_t timeout_ms)
{
    // Ensure UC8253 controller has asserted the BUSY line LOW before polling
    esp_rom_delay_us(1000);

    uint32_t elapsed = 0;
    while (epd_is_busy()) {
        vTaskDelay(pdMS_TO_TICKS(10));
        elapsed += 10;
        if (elapsed >= timeout_ms) {
            ESP_LOGW(TAG, "Timeout waiting for BUSY line (%lu ms)", (unsigned long)timeout_ms);
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

static esp_err_t write_cmd(uint8_t cmd)
{
    set_dc(0); // 0 = Command
    set_cs(0); // CS Active LOW

    s_cmd_buf[0] = cmd;
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = s_cmd_buf,
    };
    esp_err_t ret = spi_device_polling_transmit(s_spi_dev, &t);
    set_cs(1);
    return ret;
}

static esp_err_t write_byte(uint8_t data)
{
    set_dc(1); // 1 = Data
    set_cs(0);

    s_cmd_buf[0] = data;
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = s_cmd_buf,
    };
    esp_err_t ret = spi_device_polling_transmit(s_spi_dev, &t);
    set_cs(1);
    return ret;
}

static esp_err_t write_dma_buffer(const uint8_t *data, size_t len)
{
    if (len == 0 || data == NULL) return ESP_OK;
    set_dc(1); // 1 = Data
    set_cs(0); // Hold CS continuously LOW during entire buffer transfer

    size_t chunk_size = 4096;
    size_t offset = 0;

    while (offset < len) {
        size_t current_chunk = (len - offset > chunk_size) ? chunk_size : (len - offset);
        spi_transaction_t t = {
            .length = current_chunk * 8,
            .tx_buffer = data + offset,
        };
        esp_err_t ret = spi_device_polling_transmit(s_spi_dev, &t);
        if (ret != ESP_OK) {
            set_cs(1);
            return ret;
        }
        offset += current_chunk;
    }

    set_cs(1);
    return ESP_OK;
}

static void hardware_reset(void)
{
    set_res(1);
    vTaskDelay(pdMS_TO_TICKS(15));
    set_res(0);
    vTaskDelay(pdMS_TO_TICKS(20));
    set_res(1);
    vTaskDelay(pdMS_TO_TICKS(20));
    wait_until_idle(2000);
}

static void init_display_registers(void)
{
    // Panel setting: 2 bytes required (0x1F, 0x0D)
    write_cmd(CMD_PANEL_SETTING);
    write_byte(0x1F); // KW: 3f, KWR: 2f, BWROTP: 0f, BWOTP: 1f
    write_byte(0x0D);
}

__attribute__((unused)) static void set_partial_ram_area(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    uint16_t xe = (x + w - 1) | 0x0007;
    uint16_t ye = y + h - 1;
    x &= 0xFFF8;

    write_cmd(CMD_PARTIAL_WINDOW);
    write_byte(x & 0xFF);
    write_byte(xe & 0xFF);
    write_byte((y >> 8) & 0xFF);
    write_byte(y & 0xFF);
    write_byte((ye >> 8) & 0xFF);
    write_byte(ye & 0xFF);
    write_byte(0x01);
}

static void power_on(void)
{
    if (!s_power_is_on) {
        write_cmd(CMD_POWER_ON);
        wait_until_idle(1000);
        s_power_is_on = true;
    }
}

static void power_off(void)
{
    if (s_power_is_on) {
        write_cmd(CMD_POWER_OFF);
        wait_until_idle(500);
        s_power_is_on = false;
    }
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
    if (s_cmd_buf == NULL) {
        s_cmd_buf = (uint8_t *)heap_caps_malloc(32, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    }

    if (!s_buffer_curr || !s_buffer_prev || !s_cmd_buf) {
        ESP_LOGE(TAG, "Failed to allocate DMA buffers");
        return ESP_ERR_NO_MEM;
    }

    // Default: white screen (0xFF in monochrome EPD)
    memset(s_buffer_curr, 0xFF, EPD_BUFFER_SIZE);
    memset(s_buffer_prev, 0xFF, EPD_BUFFER_SIZE);

    // 2. Configure GPIOs (Manual CS, DC, RES as output)
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_config.pin_cs) | (1ULL << s_config.pin_dc) | (1ULL << s_config.pin_res),
        .mode = GPIO_MODE_OUTPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    set_cs(1); // Deselect

    gpio_config_t busy_conf = {
        .pin_bit_mask = (1ULL << s_config.pin_busy),
        .mode = GPIO_MODE_INPUT,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&busy_conf);

    // 3. Initialize SPI Bus with DMA (Manual CS for continuous transmission)
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
        .spics_io_num = -1,                 // Manual CS control
        .queue_size = 7,
    };

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi_dev);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus add device failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 4. Reset & Initialize
    hardware_reset();
    init_display_registers();

    s_partial_count = 0;
    s_is_initialized = true;

    ESP_LOGI(TAG, "EPD UC8253C initialized successfully. Resolution: %dx%d", EPD_WIDTH, EPD_HEIGHT);
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

    // Coordinate mapping: Landscape (416 x 240) -> Physical Portrait (240 x 416)
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

esp_err_t epd_update(epd_refresh_mode_t mode)
{
    if (!s_is_initialized) return ESP_ERR_INVALID_STATE;

    if (mode == EPD_REFRESH_FAST_PARTIAL && s_partial_count >= EPD_FULL_REFRESH_INTERVAL) {
        ESP_LOGI(TAG, "Partial refresh count reached %lu. Upgrading to Full Refresh.", (unsigned long)s_partial_count);
        mode = EPD_REFRESH_FULL;
    }

    int64_t t_start = esp_timer_get_time();

    if (mode == EPD_REFRESH_FULL) {
        hardware_reset();
        init_display_registers();

        // 1. Write previous and current buffers (both with curr buffer to synchronize)
        write_cmd(CMD_DATA_START_TRANS_1);
        write_dma_buffer(s_buffer_curr, EPD_BUFFER_SIZE);

        write_cmd(CMD_DATA_START_TRANS_2);
        write_dma_buffer(s_buffer_curr, EPD_BUFFER_SIZE);

        // 2. Full update waveform configuration (GoodDisplay standard)
        write_cmd(CMD_CASCADE_SETTING);
        write_byte(0x02); // TSFIX

        write_cmd(CMD_FORCE_TEMPERATURE);
        write_byte(0x5A); // 1005000us crisp waveform

        write_cmd(CMD_VCOM_DATA_INTERVAL);
        write_byte(0x97); // Border white

        power_on();

        write_cmd(CMD_DISPLAY_REFRESH);
        wait_until_idle(4000);

        power_off();
        init_display_registers(); // Undo TSFIX

        s_partial_count = 0;
    } else {
        // Fast Partial Refresh (GoodDisplay / GxEPD2 proven sequence)
        // 1. Power on analog charge pump first
        power_on();

        // 2. Configure partial waveform registers
        write_cmd(CMD_CASCADE_SETTING);
        write_byte(0x02); // TSFIX

        write_cmd(CMD_FORCE_TEMPERATURE);
        write_byte(0x6E); // Fast partial update waveform (~0.3s)

        write_cmd(CMD_VCOM_DATA_INTERVAL);
        write_byte(0xD7); // Border floating / keep during partial refresh

        // 3. Write previous buffer to DTM1 (0x10) and current buffer to DTM2 (0x13)
        write_cmd(CMD_DATA_START_TRANS_1);
        write_dma_buffer(s_buffer_prev, EPD_BUFFER_SIZE);

        write_cmd(CMD_DATA_START_TRANS_2);
        write_dma_buffer(s_buffer_curr, EPD_BUFFER_SIZE);

        // 4. Trigger master activation refresh
        write_cmd(CMD_DISPLAY_REFRESH);
        wait_until_idle(2500);

        // 5. Power off and reset panel settings
        power_off();
        init_display_registers(); // Undo TSFIX

        s_partial_count++;
    }

    memcpy(s_buffer_prev, s_buffer_curr, EPD_BUFFER_SIZE);

    int64_t elapsed_ms = (esp_timer_get_time() - t_start) / 1000;
    ESP_LOGI(TAG, "Update finished in %lld ms (Mode: %s, Count: %lu)",
             elapsed_ms, (mode == EPD_REFRESH_FULL) ? "FULL" : "PARTIAL", (unsigned long)s_partial_count);

    return ESP_OK;
}

esp_err_t epd_deep_sleep(void)
{
    if (!s_is_initialized) return ESP_ERR_INVALID_STATE;
    power_off();

    write_cmd(CMD_DEEP_SLEEP);
    write_byte(0xA5);
    return ESP_OK;
}

uint32_t epd_get_partial_count(void)
{
    return s_partial_count;
}
