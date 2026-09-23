// 서비스(생산자) ↔ UI(소비자) 사이의 공유 데이터 모델과 이벤트.
// 서비스 태스크는 app_state_set_*() 로 값을 쓰면 해당 *_UPDATED 이벤트가 자동 발행되고,
// UI 는 이벤트를 받아 LVGL lock 안에서 app_state_get_*() 스냅샷으로 화면을 갱신한다.
// 반대 방향(UI → 서비스) 요청도 이벤트로 보내 UI 가 서비스 구현에 의존하지 않게 한다.
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
    // 서비스 → UI
    APP_EVT_WIFI_CONNECTED,
    APP_EVT_WIFI_DISCONNECTED,
    APP_EVT_TIME_SYNCED,
    APP_EVT_WEATHER_UPDATED,
    APP_EVT_STOCKS_UPDATED,
    APP_EVT_MEDIA_UPDATED,       // 곡 정보/재생 상태 변경
    APP_EVT_BLE_STATE,           // data: ble_state_t (media_ble.h)
    APP_EVT_PHOTO_READY,         // data: const photo_frame_t * (photo.h)
    APP_EVT_OTA_AVAILABLE,       // data: ota_release_t (ota.h) → 팝업
    APP_EVT_OTA_PROGRESS,        // data: int 0..100
    APP_EVT_OTA_DONE,            // data: esp_err_t

    // UI → 서비스
    APP_EVT_REQ_REFRESH,         // 날씨/주가 즉시 갱신
    APP_EVT_REQ_MEDIA_CMD,       // data: media_cmd_t (media_ble.h)
    APP_EVT_REQ_PHOTO_NEXT,
    APP_EVT_REQ_PHOTO_PREV,
    APP_EVT_REQ_OTA_START,       // 팝업에서 "업데이트" 선택
    APP_EVT_UI_MODE_CHANGED,     // data: ui_mode_t (app_storage.h) → 설정 저장
    APP_EVT_SETTINGS_CHANGED,    // 설정 페이지에서 저장됨 → 서비스가 settings_load() 로 다시 읽음
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

esp_err_t app_state_init(void);   // 기본 이벤트 루프 생성 포함

// 이벤트 발행 헬퍼 (data 는 복사됨, 대기하지 않음)
esp_err_t app_event_post(app_event_id_t id, const void *data, size_t size);

void app_state_set_weather(const weather_info_t *w);
void app_state_get_weather(weather_info_t *out);

void app_state_set_stocks(const stock_list_t *s);
void app_state_get_stocks(stock_list_t *out);

void app_state_set_media(const media_info_t *m);
void app_state_get_media(media_info_t *out);

#ifdef __cplusplus
}
#endif
