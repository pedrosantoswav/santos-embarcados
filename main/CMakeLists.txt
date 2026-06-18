idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        protocol_examples_common
        driver
        esp_driver_gpio
        esp_driver_gptimer
        esp_driver_ledc
        esp_driver_i2c
        esp_lcd
        esp_timer
        esp_adc
        lvgl
        mqtt
        nvs_flash
        esp_event
        esp_netif
        spi_flash
)
