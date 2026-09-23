// LVGL UI 루트. 모든 LVGL 호출은 LVGL 태스크 또는 lvgl_port_lock() 안에서만 수행한다.
//
// 화면 구조
//   lv_layer_top : status_bar (Wi-Fi/BLE, 날짜·시각, 모드 전환, 설정) + OTA 팝업
//   lv_screen    : widget_view (카드 그리드) | slide_view (tileview) | settings (설정 페이지)
#pragma once

#include "esp_err.h"
#include "board.h"
#include "app_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

// 회전 0° 이면 board 를 프레임버퍼 2개로, 그 외에는 1개로 초기화해야 한다 (ui_board_num_fbs).
esp_err_t ui_init(const board_handles_t *hw, ui_mode_t initial_mode, ui_rotation_t rotation);
uint8_t   ui_board_num_fbs(ui_rotation_t rotation);
void      ui_set_mode(ui_mode_t mode);   // 위젯형 ↔ 슬라이드형 (애니메이션 전환)
ui_mode_t ui_get_mode(void);

#ifdef __cplusplus
}
#endif
