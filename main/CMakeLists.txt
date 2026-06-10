idf_component_register(
    SRCS "main.c"
    PRIV_REQUIRES
        mqtt
        nvs_flash
        esp_netif
        spi_flash
        esp_adc
    INCLUDE_DIRS "."
)
# (Not part of the boilerplate)
# This example uses an extra component for common functions such as Wi-Fi and Ethernet connection.
set(EXTRA_COMPONENT_DIRS
$ENV{IDF_PATH}/examples/common_components/protocol_examples_common)