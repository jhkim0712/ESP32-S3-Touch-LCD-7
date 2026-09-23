// LVGL 포팅: esp_lvgl_port 로 RGB 패널과 GT911 을 LVGL 에 연결한다.
//
// 렌더링 방식 (800x480 RGB565, PSRAM 프레임버퍼 2개)
//   - direct_mode + avoid_tearing: LVGL 이 RGB 드라이버의 프레임버퍼에 직접 그리고
//     VSYNC 에 맞춰 두 버퍼를 교대로 표시한다. 별도 draw buffer 가 없어 PSRAM 복사가 줄어든다.
//   - bb_mode: RGB 드라이버의 bounce buffer 사용 (board_lcd.c)

#include "ui.h"
#include "ui_internal.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ui";

static lv_display_t *s_disp;
static ui_mode_t     s_mode;

esp_err_t ui_init(const board_handles_t *hw, ui_mode_t initial_mode)
{
    ESP_RETURN_ON_FALSE(hw && hw->lcd_panel && hw->touch, ESP_ERR_INVALID_ARG, TAG, "hw");

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 4;
    port_cfg.task_stack = 8 * 1024;
    port_cfg.task_affinity = 1;          // core 1: 렌더링 전용 (Wi-Fi/BLE 는 core 0)
    port_cfg.task_max_sleep_ms = 500;
    port_cfg.timer_period_ms = 5;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl port init");

    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = hw->lcd_panel,
        .buffer_size = BOARD_LCD_H_RES * BOARD_LCD_V_RES,
        .double_buffer = true,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_spiram = true,
            .swap_bytes = false,
            .direct_mode = true,
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        },
    };
    s_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG, "add rgb display");

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = s_disp,
        .handle = hw->touch,
    };
    ESP_RETURN_ON_FALSE(lvgl_port_add_touch(&touch_cfg), ESP_FAIL, TAG, "add touch");

    s_mode = initial_mode;

    // [3단계] 하드웨어 확인용 진단 화면. 4단계에서 위젯/슬라이드 뷰로 교체한다.
    if (lvgl_port_lock(0)) {
        ui_diag_create(lv_screen_active());
        lvgl_port_unlock();
    }

    // 첫 프레임이 그려진 뒤 백라이트를 켜서 부팅 직후 노이즈 화면을 감춘다.
    vTaskDelay(pdMS_TO_TICKS(100));
    board_backlight_set(true);

    ESP_LOGI(TAG, "LVGL %d.%d.%d ready", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    return ESP_OK;
}

void ui_set_mode(ui_mode_t mode)
{
    s_mode = mode;   // [4단계] 위젯형/슬라이드형 전환 애니메이션
}

ui_mode_t ui_get_mode(void)
{
    return s_mode;
}
