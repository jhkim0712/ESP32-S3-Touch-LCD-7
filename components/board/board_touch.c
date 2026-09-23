// GT911 정전식 터치.
// RST 핀이 CH422G(EXIO1)에 있어 드라이버 내장 리셋을 쓸 수 없으므로,
// board_touch_reset() 에서 직접 리셋 시퀀스를 수행하고 드라이버에는 RST/INT 를 NC 로 넘긴다.
// (LVGL 은 주기적으로 좌표를 폴링하므로 INT 인터럽트는 사용하지 않는다)

#include "board_internal.h"
#include "board_pins.h"
#include "ch422g.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_touch_gt911.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "board_touch";

esp_err_t board_touch_reset(void)
{
    // 리셋 해제 순간 INT 가 LOW 이면 GT911 은 I2C 주소 0x5D 를 사용한다.
    const gpio_config_t int_out = {
        .pin_bit_mask = BIT64(BOARD_TOUCH_PIN_INT),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&int_out), TAG, "INT out");

    ESP_RETURN_ON_ERROR(ch422g_set_bits(BOARD_EXIO_TP_RST, false), TAG, "RST low");
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(BOARD_TOUCH_PIN_INT, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_RETURN_ON_ERROR(ch422g_set_bits(BOARD_EXIO_TP_RST, true), TAG, "RST high");
    vTaskDelay(pdMS_TO_TICKS(60));

    // 방향만 바꾼다 (gpio_config 재호출 시 IDF 6 이 "conflict found" 경고를 낸다)
    ESP_RETURN_ON_ERROR(gpio_set_direction(BOARD_TOUCH_PIN_INT, GPIO_MODE_INPUT), TAG, "INT in");
    vTaskDelay(pdMS_TO_TICKS(50));   // GT911 펌웨어 기동 대기
    return ESP_OK;
}

esp_err_t board_touch_init(i2c_master_bus_handle_t bus, esp_lcd_touch_handle_t *out_touch)
{
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_GT911_CONFIG();
    io_cfg.dev_addr = BOARD_TOUCH_I2C_ADDR;
    io_cfg.scl_speed_hz = BOARD_I2C_FREQ_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bus, &io_cfg, &io), TAG, "panel io");

    esp_lcd_touch_io_gt911_config_t gt911_cfg = {
        .dev_addr = BOARD_TOUCH_I2C_ADDR,
    };
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BOARD_LCD_H_RES,
        .y_max = BOARD_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
        .driver_data = &gt911_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_touch_new_i2c_gt911(io, &tp_cfg, out_touch), TAG, "gt911");
    ESP_LOGI(TAG, "GT911 ready at 0x%02X", BOARD_TOUCH_I2C_ADDR);
    return ESP_OK;
}
