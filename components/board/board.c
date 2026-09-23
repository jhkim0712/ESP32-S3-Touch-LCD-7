#include "board.h"
#include "board_internal.h"
#include "ch422g.h"

#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "board";

esp_err_t board_init(const board_config_t *cfg, board_handles_t *out)
{
    ESP_RETURN_ON_FALSE(cfg && out, ESP_ERR_INVALID_ARG, TAG, "cfg/out is NULL");

    // 1) I2C 버스 (GT911 + CH422G 공유, 보드에 4.7K 외부 풀업 있음)
    const i2c_master_bus_config_t bus_cfg = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_PIN_SDA,
        .scl_io_num = BOARD_I2C_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &out->i2c_bus), TAG, "i2c bus");

    // 2) IO 확장기: 패널 전원 ON, 백라이트 OFF, 터치 리셋 유지
    ESP_RETURN_ON_ERROR(ch422g_init(out->i2c_bus, BOARD_EXIO_BOOT_STATE), TAG, "ch422g");

    // 3) GT911 리셋 (주소 0x5D 고정)
    ESP_RETURN_ON_ERROR(board_touch_reset(), TAG, "touch reset");

    // 4) RGB LCD
    ESP_RETURN_ON_ERROR(board_lcd_init(cfg->lcd_num_fbs, &out->lcd_panel), TAG, "lcd");

    // 5) 터치 드라이버
    ESP_RETURN_ON_ERROR(board_touch_init(out->i2c_bus, &out->touch), TAG, "touch");

    ESP_LOGI(TAG, "board ready (EXIO=0x%02x)", ch422g_state());
    return ESP_OK;
}

void board_backlight_set(bool on)
{
    if (ch422g_set_bits(BOARD_EXIO_LCD_BL, on) != ESP_OK) {
        ESP_LOGW(TAG, "backlight %s failed", on ? "on" : "off");
    }
}
