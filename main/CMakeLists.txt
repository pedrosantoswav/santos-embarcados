idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS ".")

# (Not part of the boilerplate)
# This example uses an extra component for common functions such as Wi-Fi and Ethernet connection.
set(EXTRA_COMPONENT_DIRS
$ENV{IDF_PATH}/examples/common_components/protocol_examples_common)