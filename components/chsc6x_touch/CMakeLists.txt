idf_component_register(
    SRCS "chsc6x_touch.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_lcd_touch
    PRIV_REQUIRES esp_timer
)
