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
    int           photo_interval_s;
    char          stock_symbols[128];
} app_settings_t;

esp_err_t storage_init(void);                       // LittleFS 마운트 (실패 시 포맷)
esp_err_t settings_load(app_settings_t *out);       // NVS 값, 없으면 Kconfig 기본값
esp_err_t settings_save(const app_settings_t *in);

#ifdef __cplusplus
}
#endif
