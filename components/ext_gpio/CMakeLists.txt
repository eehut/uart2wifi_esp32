idf_component_register(SRCS "ext_gpio_drive.c" "ext_gpio.c" "ext_gpio_event.c"
                    INCLUDE_DIRS "include"
                    REQUIRES driver esp_common uptime app_event_loop)