idf_component_register(
    SRCS "board.c" "ch422g.c" "board_lcd.c" "board_touch.c" "board_sdcard.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_sdspi esp_lcd fatfs sdmmc
             esp_lcd_touch_gt911
)
