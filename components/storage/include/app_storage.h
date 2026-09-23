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

typedef struct {
    char      wifi_ssid[33];
    char      wifi_pass[65];
    ui_mode_t ui_mode;
    int       photo_interval_s;
    char      stock_symbols[128];
} app_settings_t;

esp_err_t storage_init(void);                       // LittleFS 마운트
esp_err_t settings_load(app_settings_t *out);       // NVS → Kconfig 기본값 순
esp_err_t settings_save(const app_settings_t *in);

#ifdef __cplusplus
}
#endif
