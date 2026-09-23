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

// 응답 본문을 PSRAM 버퍼로 받아온다. 호출자가 free() 해야 함.
// 인증서 검증은 esp_crt_bundle 사용.
esp_err_t http_get_alloc(const char *url, const char *extra_headers,
                         char **out_body, size_t *out_len, int *out_status);

#ifdef __cplusplus
}
#endif
