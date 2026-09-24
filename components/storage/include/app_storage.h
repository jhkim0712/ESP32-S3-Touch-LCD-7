// 내부 LittleFS(/storage) 마운트와 NVS 기반 사용자 설정.
// SD 카드 하드웨어 마운트는 board_sdcard_mount() 가 담당한다.
#pragma once

#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define STORAGE_LFS_MOUNT_POINT "/storage"   // 폰트(ttf), 아이콘, 기본 사진

typedef enum { UI_MODE_WIDGET = 0, UI_MODE_SLIDE = 1 } ui_mode_t;

// UI 언어. 한글 폰트(/storage/fonts)를 불러오지 못하면 UI 는 영어로 표시한다.
typedef enum { APP_LANG_EN = 0, APP_LANG_KO = 1 } app_lang_t;

// 화면 회전 (터치 좌표도 LVGL 이 함께 변환). 변경 시 재부팅 후 적용.
//   0      : 800x480, 프레임버퍼 2개에 직접 렌더링 + tearing 방지
//   90/270 : 480x800, 부분 버퍼 → RGB 드라이버가 회전 복사 (프레임버퍼 1개)
//   180    : 800x480, 90/270 과 같은 방식
typedef enum {
    UI_ROTATION_0   = 0,
    UI_ROTATION_90  = 1,
    UI_ROTATION_180 = 2,
    UI_ROTATION_270 = 3,
} ui_rotation_t;

typedef struct {
    char          wifi_ssid[33];
    char          wifi_pass[65];
    ui_mode_t     ui_mode;
    ui_rotation_t rotation;
    app_lang_t    language;
    int           photo_interval_s;
    char          stock_symbols[128];
} app_settings_t;

esp_err_t storage_init(void);                       // LittleFS 마운트 (실패 시 포맷)
esp_err_t settings_load(app_settings_t *out);       // NVS 값, 없으면 Kconfig 기본값
esp_err_t settings_save(const app_settings_t *in);

// Flickr 사진 피드 URL 목록 (줄바꿈 구분). app_settings_t 와 따로 둔다: 설정 구조체는 작은 스택에
// 자주 복사되므로 크기를 키우지 않는다.
#define SETTINGS_FLICKR_MAX_FEEDS   8
#define SETTINGS_FLICKR_URL_MAX     256     // NUL 포함
#define SETTINGS_FLICKR_FEEDS_LEN   (SETTINGS_FLICKR_MAX_FEEDS * SETTINGS_FLICKR_URL_MAX)
esp_err_t settings_get_flickr_feeds(char *out, size_t size);   // 없으면 ""
esp_err_t settings_set_flickr_feeds(const char *feeds);

// 웹 설정 페이지 접속 PIN (6자리 숫자). 처음 호출할 때 무작위로 만들어 NVS 에 저장한다.
esp_err_t settings_get_web_pin(char *out, size_t size);    // size >= 7

#ifdef __cplusplus
}
#endif
