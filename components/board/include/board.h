// 보드 지원 패키지(BSP) 공개 API. 구현은 3단계에서 작성.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_lcd_types.h"
#include "esp_lcd_touch.h"
#include "driver/i2c_master.h"
#include "board_pins.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_master_bus_handle_t i2c_bus;
    esp_lcd_panel_handle_t  lcd_panel;
    esp_lcd_touch_handle_t  touch;
} board_handles_t;

// I2C → CH422G → GT911 리셋 시퀀스 → RGB 패널 → 터치 순으로 초기화
esp_err_t board_init(board_handles_t *out);

void      board_backlight_set(bool on);
esp_err_t board_sdcard_mount(void);
void      board_sdcard_unmount(void);
bool      board_sdcard_is_mounted(void);

#ifdef __cplusplus
}
#endif
