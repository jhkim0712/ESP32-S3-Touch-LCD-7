// ui 컴포넌트 내부 공유 선언. 모든 함수는 LVGL 태스크 또는 lvgl_port_lock() 안에서만 호출한다.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "lvgl.h"
#include "esp_err.h"
#include "app_storage.h"
#include "ota.h"

#define UI_STATUS_BAR_H     48
#define UI_GAP              12

#define UI_FONT_S           (&lv_font_montserrat_14)
#define UI_FONT_M           (&lv_font_montserrat_20)
#define UI_FONT_L           (&lv_font_montserrat_28)
#define UI_FONT_XL          (&lv_font_montserrat_48)

#define UI_COLOR_BG         lv_color_hex(0x0E1116)
#define UI_COLOR_CARD       lv_color_hex(0x1A1F27)
#define UI_COLOR_CARD_ALT   lv_color_hex(0x242A33)
#define UI_COLOR_TEXT       lv_color_hex(0xE6EDF3)
#define UI_COLOR_DIM        lv_color_hex(0x8B949E)
#define UI_COLOR_ACCENT     lv_color_hex(0x3DA9FC)
#define UI_COLOR_WARM       lv_color_hex(0xFFB000)
// 국내 증시 관례: 상승 빨강, 하락 파랑
#define UI_COLOR_UP         lv_color_hex(0xFF5B5B)
#define UI_COLOR_DOWN       lv_color_hex(0x4DA3FF)

// ---- 데이터 subject (LVGL observer). 서비스 이벤트 → ui_core 가 갱신 ----
extern lv_subject_t ui_subj_time;       // int: epoch seconds (1초마다)
extern lv_subject_t ui_subj_weather;    // int: 갱신 카운터 → app_state_get_weather()
extern lv_subject_t ui_subj_stocks;     // int: 갱신 카운터 → app_state_get_stocks()
extern lv_subject_t ui_subj_media;      // int: 갱신 카운터 → app_state_get_media()
extern lv_subject_t ui_subj_photo;      // pointer: const photo_frame_t *
extern lv_subject_t ui_subj_wifi;       // int: 0/1
extern lv_subject_t ui_subj_ble;        // int: ble_state_t

// ---- theme / 공통 ----
void      ui_theme_init(lv_display_t *disp);
lv_obj_t *ui_screen_create(void);                       // 상태 표시줄 아래 영역을 쓰는 빈 화면
lv_obj_t *ui_card_create(lv_obj_t *parent);
lv_obj_t *ui_card_title(lv_obj_t *card, const char *symbol, const char *text);
lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text);
bool      ui_is_portrait(void);
int32_t   ui_content_height(void);                      // 화면 높이 - 상태 표시줄
void      ui_fmt_number(char *buf, size_t size, double value, int decimals);
void      ui_free_user_data_on_delete(lv_obj_t *obj);   // obj 삭제 시 user_data 를 lv_free

// ---- 페이지 구성요소 (full: 슬라이드 페이지, false: 위젯 카드) ----
void ui_clock_create(lv_obj_t *parent, bool full);
void ui_weather_create(lv_obj_t *parent, bool full);
void ui_stocks_create(lv_obj_t *parent, bool full);
void ui_music_create(lv_obj_t *parent, bool full);
void ui_photo_create(lv_obj_t *parent);

// ---- 화면 / 오버레이 ----
lv_obj_t *ui_widget_view_create(void);
lv_obj_t *ui_slide_view_create(void);
void      ui_status_bar_create(void);
void      ui_status_bar_set_mode(ui_mode_t mode);
void      ui_ota_popup_show(const ota_release_t *rel);
void      ui_ota_popup_progress(int percent);
void      ui_ota_popup_done(esp_err_t result);
