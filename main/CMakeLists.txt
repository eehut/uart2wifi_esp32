idf_component_register(SRCS "serial2ip_esp32.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_common uptime)
