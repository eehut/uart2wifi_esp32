idf_component_register(SRCS "uart_bridge.c" "board.c" "cli_impl.c" "cli_menu.c" "display.c" "img_icons.c" "app_main.c" 
                    INCLUDE_DIRS "include"
                    REQUIRES driver esp_common nvs_flash uptime ext_gpio app_event_loop bus_manager lcd_font lcd_display wifi_station tcp_server misc_utils)


# 为ext_gpio组件设置编译宏
idf_component_get_property(ext_gpio ext_gpio COMPONENT_LIB)
target_compile_definitions(${ext_gpio} PRIVATE
                    CONFIG_EXT_GPIO_MAX_NUM=4
                    CONFIG_EXT_BUTTON_MAX_NUM=2
                    CONFIG_EXT_GPIO_CACHE_SIZE=4
                    CONFIG_EXT_BUTTON_DEFAULT_LONG_PRESSED_SECOND=2
)
