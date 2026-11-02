idf_component_register(
    SRCS "src/ui_lvgl.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl epd_uc8253c esp_timer freertos log
)

