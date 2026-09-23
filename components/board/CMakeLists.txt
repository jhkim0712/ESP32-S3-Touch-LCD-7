# SRCS to be added in step 3: ch422g.c board.c board_lcd.c board_touch.c board_sdcard.c
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi esp_lcd fatfs
             esp_lcd_touch_gt911
)
