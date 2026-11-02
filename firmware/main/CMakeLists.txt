idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp_event esp_system nvs_flash log spi_flash epd_uc8253c ui_lvgl
)
