idf_component_register(
    SRCS "lcd_driver.c" "lcd_display.c"
    INCLUDE_DIRS "include"
    REQUIRES driver lcd_font bus_manager uptime
)
