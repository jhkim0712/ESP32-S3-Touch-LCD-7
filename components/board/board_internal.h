// board 컴포넌트 내부 함수 (외부 공개 안 함)
#pragma once

#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"
#include "driver/i2c_master.h"

esp_err_t board_lcd_init(uint8_t num_fbs, esp_lcd_panel_handle_t *out_panel);
esp_err_t board_touch_reset(void);
esp_err_t board_touch_init(i2c_master_bus_handle_t bus, esp_lcd_touch_handle_t *out_touch);
