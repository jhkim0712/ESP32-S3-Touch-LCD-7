// 전자앨범: SD(/sdcard/photos, 없으면 /storage/photos)와 그 하위 폴더(4단계까지)의
// JPG/PNG 를 화면 크기에 맞게 줄여
// PSRAM 에 디코딩하고, 더블 버퍼로 UI 에 넘긴다 (APP_EVT_PHOTO_READY). UI 는 표시만 한다.
//   - JPEG: ROM TJpgDec 로 파일을 조금씩 읽으며 바로 축소 → 수 MB 사진도 메모리에 올리지 않음
//           EXIF 방향(휴대폰 세로 사진) 반영, progressive JPEG 는 미지원
//   - PNG : LVGL 의 lodepng 사용, 전체를 메모리에 풀어야 하므로 800x600 이하만
// 요청 이벤트: APP_EVT_REQ_PHOTO_NEXT / _PREV, APP_EVT_SETTINGS_CHANGED(간격)
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t *pixels;      // RGB565, PSRAM, stride = width
    uint16_t  width;
    uint16_t  height;
    char      path[256];     // 하위 폴더 포함 전체 경로
} photo_frame_t;

// max_w/max_h: 사진을 표시할 영역 크기 (회전 반영). core 1, 낮은 우선순위 태스크.
esp_err_t photo_start(const char *dir, int interval_s, uint16_t max_w, uint16_t max_h);

#ifdef __cplusplus
}
#endif
