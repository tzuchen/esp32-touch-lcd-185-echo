idf_component_register(
    SRCS "board_lcd.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_lcd esp_io_expander esp_io_expander_tca9554
)
