// 전자앨범: SD(/sdcard/photos) 또는 LittleFS 의 JPG/PNG 를 PSRAM 에 미리 디코딩해
// 더블 버퍼로 UI 에 넘긴다. UI 는 표시만 하고 디코딩은 하지 않는다.
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t *pixels;      // RGB565, PSRAM, 화면 크기 이하로 축소됨
    uint16_t  width;
    uint16_t  height;
    char      path[128];
} photo_frame_t;

esp_err_t            photo_start(const char *dir, int interval_s);  // core 1, 낮은 우선순위
void                 photo_set_interval(int interval_s);
void                 photo_next(void);
void                 photo_prev(void);
const photo_frame_t *photo_current(void);   // APP_EVT_PHOTO_READY 이후 유효

#ifdef __cplusplus
}
#endif
