idf_component_register(SRCS "ext_gpio_drive.c" "ext_gpio.c"
                    INCLUDE_DIRS .
                    REQUIRES driver esp_common uptime)