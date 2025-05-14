idf_component_register(SRCS "serial2ip_esp32.c"
                    INCLUDE_DIRS "."
                    REQUIRES driver esp_common uptime ext_gpio app_event_loop lcd_display)

# 为ext_gpio组件设置编译宏
idf_component_get_property(ext_gpio ext_gpio COMPONENT_LIB)
target_compile_definitions(${ext_gpio} PRIVATE
                    CONFIG_EXT_GPIO_MAX_NUM=4
                    CONFIG_EXT_BUTTON_MAX_NUM=2
                    CONFIG_EXT_GPIO_CACHE_SIZE=4
)
