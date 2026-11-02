idf_component_register(
    SRCS "src/input_encoder.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_pcnt esp_driver_gpio esp_driver_uart esp_event esp_timer freertos log
)
