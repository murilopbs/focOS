idf_component_register(
    SRCS "src/epd_uc8253c.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer freertos log
)
