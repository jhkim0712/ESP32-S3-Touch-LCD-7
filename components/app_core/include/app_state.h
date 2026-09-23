// 서비스(생산자) ↔ UI(소비자) 사이의 공유 데이터 모델과 이벤트.
// 서비스 태스크는 app_state_update_*() 로 값을 쓰고 이벤트를 발행하며,
// UI 는 이벤트를 받아 LVGL lock 안에서 스냅샷을 읽어 화면을 갱신한다.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(APP_EVENT);

typedef enum {
    APP_EVT_WIFI_CONNECTED,
    APP_EVT_WIFI_DISCONNECTED,
    APP_EVT_TIME_SYNCED,
    APP_EVT_WEATHER_UPDATED,
    APP_EVT_STOCKS_UPDATED,
    APP_EVT_MEDIA_UPDATED,       // 곡 정보/재생 상태 변경
    APP_EVT_BLE_STATE,           // 연결/페어링 상태
    APP_EVT_PHOTO_READY,         // 다음 사진 디코딩 완료
    APP_EVT_OTA_AVAILABLE,       // 새 버전 발견 → 팝업
    APP_EVT_OTA_PROGRESS,
    APP_EVT_OTA_DONE,
} app_event_id_t;

#define APP_MAX_STOCKS 8

typedef struct {
    bool     valid;
    float    temp_c;
    int      humidity;
    int      wmo_code;           // Open-Meteo WMO weather code → 아이콘 매핑
    bool     is_day;
    time_t   updated_at;
} weather_info_t;

typedef struct {
    char     symbol[16];
    char     name[24];
    double   price;
    double   change_pct;
    char     currency[4];
} stock_quote_t;

typedef struct {
    int           count;
    stock_quote_t items[APP_MAX_STOCKS];
    time_t        updated_at;
} stock_list_t;

typedef enum { MEDIA_STOPPED, MEDIA_PLAYING, MEDIA_PAUSED } media_play_state_t;

typedef struct {
    char               title[128];   // UTF-8 (한글 폰트 필요)
    char               artist[96];
    char               album[96];
    media_play_state_t state;
    uint32_t           duration_s;
    uint32_t           elapsed_s;
} media_info_t;

esp_err_t app_state_init(void);

void app_state_set_weather(const weather_info_t *w);
void app_state_get_weather(weather_info_t *out);

void app_state_set_stocks(const stock_list_t *s);
void app_state_get_stocks(stock_list_t *out);

void app_state_set_media(const media_info_t *m);
void app_state_get_media(media_info_t *out);

#ifdef __cplusplus
}
#endif
