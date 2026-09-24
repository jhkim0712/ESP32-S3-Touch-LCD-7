// GitHub Releases 기반 OTA.
//  1) GET https://api.github.com/repos/{owner}/{repo}/releases/latest   (ota_check, net_worker 에서)
//  2) tag_name(vX.Y.Z) 과 esp_app_desc.version 을 semver 비교
//  3) 새 버전이면 APP_EVT_OTA_AVAILABLE (태그마다 한 번) → UI 팝업 / 웹 설정 페이지
//  4) 사용자가 승인하면 APP_EVT_REQ_OTA_START → net_worker 가 ota_install():
//     assets[] 중 CONFIG_APP_OTA_ASSET_NAME 의 browser_download_url 을 esp_https_ota 로 받아 쓴다
//     (GitHub 는 다른 호스트로 리다이렉트). 진행률 APP_EVT_OTA_PROGRESS, 결과 APP_EVT_OTA_DONE → 성공 시 재부팅
//  5) 새 펌웨어는 60초 동안 정상 동작하면 유효로 표시한다. 그 전에 멈추거나 재부팅되면 부트로더가 이전 펌웨어로 되돌린다.
// 확인과 설치 모두 net_worker 태스크에서 실행해 TLS 세션을 동시에 하나만 연다.
#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char tag[32];
    char asset_url[256];
    int  asset_size;
    char notes[512];        // release body 앞부분 (팝업 표시용, UTF-8 경계에서 자름)
} ota_release_t;

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_CHECKING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_DONE,         // 설치 완료, 곧 재부팅
    OTA_STATE_FAILED,
} ota_state_t;

typedef struct {
    ota_state_t   state;
    bool          has_latest;      // 마지막 확인에 성공했음 (latest 유효)
    bool          available;       // latest 가 지금 버전보다 새 버전
    ota_release_t latest;
    int           progress;        // 0..100 (DOWNLOADING)
    char          error[48];       // 마지막 확인/설치 실패 이유
    time_t        last_check;      // 0 = 아직 확인 안 함
} ota_status_t;

esp_err_t ota_init(void);                  // 부팅 시: 롤백 확인 타이머 시작
esp_err_t ota_check(void);                 // net_worker 에서 호출
esp_err_t ota_install(void);               // net_worker 에서 호출 (확인된 최신 릴리스, 수 분 걸림)
void      ota_get_status(ota_status_t *out);

#ifdef __cplusplus
}
#endif
