// ui 컴포넌트 내부 공유 선언. 모든 함수는 LVGL 태스크 또는 lvgl_port_lock() 안에서만 호출한다.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <time.h>
#include "lvgl.h"
#include "esp_err.h"
#include "app_storage.h"
#include "ota.h"

#define UI_STATUS_BAR_H     48
#define UI_GAP              12

// 본문 글꼴: 한글 TTF(Tiny TTF, /storage/fonts) → 없으면 Montserrat. LV_SYMBOL_* 기호는
// 한글 폰트의 fallback 으로 연결한 Montserrat 에서 그린다.
// XL(48px)은 시계/기온 숫자에만 쓰므로 Montserrat 그대로 사용.
extern const lv_font_t *ui_font_s, *ui_font_m, *ui_font_l;
#define UI_FONT_S           (ui_font_s)
#define UI_FONT_M           (ui_font_m)
#define UI_FONT_L           (ui_font_l)
#define UI_FONT_XL          (&lv_font_montserrat_48)

// 날씨 아이콘 폰트 (Weather Icons TTF). 파일이 없으면 NULL → 색 원으로 대신 표시
extern const lv_font_t *ui_font_weather_l, *ui_font_weather_s;

// ---- 언어 ----
app_lang_t ui_lang(void);       // 한글 폰트가 없으면 항상 APP_LANG_EN
#define TR(en, ko)          (ui_lang() == APP_LANG_KO ? (ko) : (en))
// 날짜: long_form = "2026년 9월 23일 수요일" / "Wednesday, 23 September 2026"
//       short     = "9월 23일 (수)"          / "Wed 23 Sep"
void ui_fmt_date(char *buf, size_t size, const struct tm *tm, bool long_form);

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
extern lv_subject_t ui_subj_wifi;       // int: 0/1 (IP 는 app_state_get_ip)
extern lv_subject_t ui_subj_wifi_scan;  // int: 스캔 완료 카운터 → app_state_get_wifi_scan()
extern lv_subject_t ui_subj_ble;        // int: ble_state_t

// ---- theme / 공통 ----
void      ui_fonts_init(void);                          // 한글/아이콘 TTF 로드 (LVGL lock 안에서)
void      ui_theme_init(lv_display_t *disp);
void      ui_lang_set(app_lang_t lang);
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
void      ui_show_home(lv_screen_load_anim_t anim);    // 현재 모드 화면을 새로 만들어 표시
void      ui_settings_open(void);
bool      ui_settings_is_open(void);
ui_rotation_t ui_active_rotation(void);                 // 이번 부팅에 적용된 회전
void      ui_status_bar_create(void);
void      ui_status_bar_set_mode(ui_mode_t mode);
void      ui_ota_popup_show(const ota_release_t *rel);
void      ui_ota_popup_progress(int percent);
void      ui_ota_popup_done(esp_err_t result);
