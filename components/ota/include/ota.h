// GitHub Releases 기반 OTA.
//  1) GET https://api.github.com/repos/{owner}/{repo}/releases/latest
//  2) tag_name(vX.Y.Z) 과 esp_app_desc.version 을 semver 비교
//  3) 새 버전이면 APP_EVT_OTA_AVAILABLE → UI 팝업 → 사용자 승인 시
//  4) assets[].browser_download_url 을 esp_https_ota 로 다운로드 (리다이렉트 추적)
//  5) 재부팅 후 정상 동작 확인 시 ota_mark_app_valid() → 실패 시 자동 롤백
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char tag[32];
    char asset_url[256];
    int  asset_size;
    char notes[512];        // release body 앞부분 (팝업 표시용)
} ota_release_t;

esp_err_t ota_check_latest(ota_release_t *out, bool *is_newer);  // net_worker 에서 호출
esp_err_t ota_start_update(const ota_release_t *rel);            // 백그라운드 태스크 생성
void      ota_mark_app_valid(void);                              // 부팅 self-test 통과 후

#ifdef __cplusplus
}
#endif
