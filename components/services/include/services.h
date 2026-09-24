// 네트워크 데이터 수집기: 날씨(OpenWeatherMap 또는 Open-Meteo), 주가(Yahoo / Alpha Vantage).
// 하나의 net_worker 태스크가 요청을 직렬로 처리해 동시 TLS 세션 수를 1개로 제한한다.
#pragma once

#include "esp_err.h"
#include "app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WEATHER_STATUS_NONE,            // 아직 요청 안 함
    WEATHER_STATUS_OK,
    WEATHER_STATUS_NET_ERROR,       // 연결 실패
    WEATHER_STATUS_BAD_KEY,         // OWM 401: API 키가 틀리거나 아직 활성화 전 (발급 후 최대 2시간)
    WEATHER_STATUS_CITY_NOT_FOUND,  // OWM 404
    WEATHER_STATUS_HTTP_ERROR,
    WEATHER_STATUS_PARSE_ERROR,
} weather_status_t;

// owm_api_key 가 있으면 OpenWeatherMap(owm_city: 도시 ID / "이름,국가" / "" = Kconfig 좌표), 없으면 Open-Meteo
esp_err_t weather_fetch(const char *owm_api_key, const char *owm_city, weather_info_t *out);
weather_status_t weather_last_status(void);   // 마지막 weather_fetch 결과 (아무 태스크에서나)
esp_err_t stocks_fetch(const char *symbols_csv, stock_list_t *out);

// Wi-Fi 연결 이벤트 이후 주기적으로 weather/stocks 를 갱신 (core 0)
esp_err_t net_worker_start(void);
void      net_worker_request_refresh(void);   // UI 새로고침 버튼용

#ifdef __cplusplus
}
#endif
