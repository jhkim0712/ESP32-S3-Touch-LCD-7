// LVGL UI 루트. 모든 LVGL 호출은 LVGL 태스크 또는 lvgl_port_lock() 안에서만 수행한다.
//
// 화면 구조
//   ui_root (lv_screen)
//    ├─ widget_view : 2x2 카드 그리드 (시계 | 날씨+주가 / 음악 | 사진 썸네일)
//    ├─ slide_view  : lv_tileview 가로 스와이프 (시계 → 앨범 → 날씨/주가 → 음악)
//    ├─ status_bar  : Wi-Fi/BLE 아이콘, 모드 전환 버튼, 설정
//    └─ overlays    : OTA 팝업, Wi-Fi 설정 키보드
#pragma once

#include "esp_err.h"
#include "board.h"
#include "app_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ui_init(const board_handles_t *hw, ui_mode_t initial_mode);
void      ui_set_mode(ui_mode_t mode);   // 위젯형 ↔ 슬라이드형 (애니메이션 전환)
ui_mode_t ui_get_mode(void);

#ifdef __cplusplus
}
#endif
