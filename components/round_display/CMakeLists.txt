idf_component_register(SRCS "round_display.c" INCLUDE_DIRS "include"
    REQUIRES driver esp_lcd esp_lcd_gc9a01 esp_lcd_touch esp_lvgl_port chsc6x_touch lvgl)
