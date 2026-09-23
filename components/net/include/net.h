// Wi-Fi 연결 관리, SNTP(KST) 동기화, HTTPS GET 헬퍼.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_mgr_start(const char *ssid, const char *pass);  // 자동 재연결 포함
bool      wifi_mgr_is_connected(void);
bool      wifi_mgr_wait_connected(uint32_t timeout_ms);

esp_err_t time_sync_start(void);         // TZ=KST-9 설정 후 SNTP 시작
bool      time_is_synced(void);

// 응답 본문을 PSRAM 버퍼로 받아온다 (NUL 종료). 성공 시 호출자가 free() 해야 함.
// 인증서 검증은 esp_crt_bundle, 리다이렉트는 최대 5회 따라간다.
// extra_headers: 줄바꿈(CRLF)으로 구분한 "Name: value" 목록 (NULL 가능).
// 반환값 ESP_OK 는 "통신 성공"이며, HTTP 상태 코드(4xx/5xx 포함)는 out_status 로 확인한다.
esp_err_t http_get_alloc(const char *url, const char *extra_headers,
                         char **out_body, size_t *out_len, int *out_status);

#ifdef __cplusplus
}
#endif
