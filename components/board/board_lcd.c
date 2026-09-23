// ST7262 RGB 패널. 컨트롤러 초기화 명령이 없는 순수 RGB(DE 모드) 패널이라
// esp_lcd RGB 드라이버 설정만으로 동작한다.
//
// - 프레임버퍼 2개(각 750KB)는 PSRAM, LVGL 이 여기에 직접 그린다 (tearing 방지)
// - DMA 는 내부 RAM bounce buffer 에서 읽는다 → Wi-Fi/Flash 쓰기로 PSRAM 대역폭이
//   흔들려도 화면 밀림(drift)이 생기지 않는다.

#include "board_internal.h"
#include "board_pins.h"

#include "esp_check.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_rgb.h"

static const char *TAG = "board_lcd";

esp_err_t board_lcd_init(esp_lcd_panel_handle_t *out_panel)
{
    const esp_lcd_rgb_panel_config_t cfg = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .timings = {
            .pclk_hz = BOARD_LCD_PCLK_HZ,
            .h_res = BOARD_LCD_H_RES,
            .v_res = BOARD_LCD_V_RES,
            .hsync_pulse_width = BOARD_LCD_HSYNC_PULSE,
            .hsync_back_porch = BOARD_LCD_HSYNC_BACK_PORCH,
            .hsync_front_porch = BOARD_LCD_HSYNC_FRONT_PORCH,
            .vsync_pulse_width = BOARD_LCD_VSYNC_PULSE,
            .vsync_back_porch = BOARD_LCD_VSYNC_BACK_PORCH,
            .vsync_front_porch = BOARD_LCD_VSYNC_FRONT_PORCH,
            .flags.pclk_active_neg = BOARD_LCD_PCLK_ACTIVE_NEG,
        },
        .data_width = 16,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .num_fbs = 2,
        .bounce_buffer_size_px = BOARD_LCD_H_RES * BOARD_LCD_BOUNCE_LINES,
        .dma_burst_size = 64,
        .hsync_gpio_num = BOARD_LCD_PIN_HSYNC,
        .vsync_gpio_num = BOARD_LCD_PIN_VSYNC,
        .de_gpio_num = BOARD_LCD_PIN_DE,
        .pclk_gpio_num = BOARD_LCD_PIN_PCLK,
        .disp_gpio_num = BOARD_LCD_PIN_DISP_EN,
        .data_gpio_nums = {
            BOARD_LCD_PIN_DATA0,  BOARD_LCD_PIN_DATA1,  BOARD_LCD_PIN_DATA2,  BOARD_LCD_PIN_DATA3,
            BOARD_LCD_PIN_DATA4,  BOARD_LCD_PIN_DATA5,  BOARD_LCD_PIN_DATA6,  BOARD_LCD_PIN_DATA7,
            BOARD_LCD_PIN_DATA8,  BOARD_LCD_PIN_DATA9,  BOARD_LCD_PIN_DATA10, BOARD_LCD_PIN_DATA11,
            BOARD_LCD_PIN_DATA12, BOARD_LCD_PIN_DATA13, BOARD_LCD_PIN_DATA14, BOARD_LCD_PIN_DATA15,
        },
        .flags.fb_in_psram = 1,
    };

    esp_lcd_panel_handle_t panel = NULL;
    ESP_RETURN_ON_ERROR(esp_lcd_new_rgb_panel(&cfg, &panel), TAG, "new rgb panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "init");

    ESP_LOGI(TAG, "RGB panel %dx%d @ %d MHz, 2 PSRAM FBs, bounce %d lines",
             BOARD_LCD_H_RES, BOARD_LCD_V_RES, BOARD_LCD_PCLK_HZ / 1000000, BOARD_LCD_BOUNCE_LINES);
    *out_panel = panel;
    return ESP_OK;
}
