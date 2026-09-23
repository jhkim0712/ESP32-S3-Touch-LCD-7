// 네트워크 데이터 수집기: 날씨(Open-Meteo), 주가(Yahoo / Alpha Vantage).
// 하나의 net_worker 태스크가 요청을 직렬로 처리해 동시 TLS 세션 수를 1개로 제한한다.
#pragma once

#include "esp_err.h"
#include "app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t weather_fetch(weather_info_t *out);
esp_err_t stocks_fetch(const char *symbols_csv, stock_list_t *out);

// Wi-Fi 연결 이벤트 이후 주기적으로 weather/stocks 를 갱신 (core 0)
esp_err_t net_worker_start(void);
void      net_worker_request_refresh(void);   // UI 새로고침 버튼용

#ifdef __cplusplus
}
#endif
