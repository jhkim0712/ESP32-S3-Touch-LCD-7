// 네트워크 데이터 수집 태스크 (core 0)
// HTTPS 요청을 이 태스크 하나에서 순서대로 처리해 TLS 세션이 동시에 하나만 열리게 한다
// (TLS 세션 하나가 내부 RAM 을 수십 KB 사용).
//   - 날씨: CONFIG_APP_WEATHER_REFRESH_MIN 마다, 실패 시 1분 후 재시도
//   - 주가: CONFIG_APP_STOCK_REFRESH_SEC 마다, 실패 시 30초 후 재시도
//   - Flickr 사진 피드: CONFIG_APP_FLICKR_SYNC_MIN 마다, 실패 시 10분 후 재시도 (app_flickr_sync)
//     Wi-Fi 가 끊겨 있으면 삭제된 피드의 폴더 정리만 한다.
//   - 펌웨어 업데이트 확인: CONFIG_APP_OTA_CHECK_INTERVAL_H 마다 (연결 직후, APP_EVT_REQ_OTA_CHECK 시 즉시)
//     설치(APP_EVT_REQ_OTA_START)는 다른 작업보다 먼저 이 태스크에서 실행한다 (끝나면 재부팅)
//   - Wi-Fi 연결 직후, 새로고침 요청, 설정 변경(종목) 시 즉시 갱신 (Flickr 는 연결 직후와 APP_EVT_REQ_FLICKR_SYNC)

#include "services.h"

#include <stdlib.h>
#include "app_flickr.h"
#include "app_storage.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.h"
#include "ota.h"
#include "sdkconfig.h"

#define NOTIFY_REFRESH      BIT0
#define NOTIFY_SETTINGS     BIT1
#define NOTIFY_FLICKR       BIT2
#define NOTIFY_OTA_CHECK    BIT3
#define NOTIFY_OTA_INSTALL  BIT4
#define WEATHER_RETRY_MS    (60 * 1000)
#define STOCKS_RETRY_MS     (30 * 1000)
#define FLICKR_RETRY_MS     (10 * 60 * 1000)
#define OTA_RETRY_MS        (30 * 60 * 1000)
#define OFFLINE_WAIT_MS     (60 * 60 * 1000)
#define TASK_STACK          (12 * 1024)   // TLS 핸드셰이크(ECDHE) + Flickr 다운로드 / esp_https_ota 호출 깊이
#define STACK_WARN_BYTES    1536

static const char *TAG = "net_worker";

static TaskHandle_t s_task;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void on_app_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (!s_task) {
        return;
    }
    switch (id) {
    case APP_EVT_WIFI_CONNECTED:
        xTaskNotify(s_task, NOTIFY_REFRESH | NOTIFY_FLICKR | NOTIFY_OTA_CHECK, eSetBits);
        break;
    case APP_EVT_REQ_REFRESH:
        xTaskNotify(s_task, NOTIFY_REFRESH, eSetBits);
        break;
    case APP_EVT_REQ_FLICKR_SYNC:
        xTaskNotify(s_task, NOTIFY_FLICKR, eSetBits);
        break;
    case APP_EVT_SETTINGS_CHANGED:
        xTaskNotify(s_task, NOTIFY_SETTINGS, eSetBits);
        break;
    case APP_EVT_REQ_OTA_CHECK:
        xTaskNotify(s_task, NOTIFY_OTA_CHECK, eSetBits);
        break;
    case APP_EVT_REQ_OTA_START:
        xTaskNotify(s_task, NOTIFY_OTA_INSTALL, eSetBits);
        break;
    default:
        break;
    }
}

// 무거운 작업 뒤 남은 스택을 기록한다 (넘치면 옆 메모리를 덮어써 엉뚱한 곳에서 멈추므로 미리 확인)
static void log_stack(const char *after)
{
    unsigned left = (unsigned)uxTaskGetStackHighWaterMark(NULL);
    if (left < STACK_WARN_BYTES) {
        ESP_LOGW(TAG, "stack nearly full after %s: %u bytes left", after, left);
    } else {
        ESP_LOGI(TAG, "stack after %s: %u bytes left", after, left);
    }
}

static void worker(void *arg)
{
    app_settings_t *settings = calloc(1, sizeof(app_settings_t));
    stock_list_t *stocks = calloc(1, sizeof(stock_list_t));
    configASSERT(settings && stocks);
    settings_load(settings);

    int64_t next_weather = 0, next_stocks = 0, next_flickr = 0, next_ota = 0;
    for (;;) {
        int64_t next = next_weather < next_stocks ? next_weather : next_stocks;
        next = next_flickr < next ? next_flickr : next;
        next = next_ota < next ? next_ota : next;
        int64_t wait = next - now_ms();
        if (wait < 0) {
            wait = 0;
        }
        uint32_t bits = 0;
        xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(wait > 3600000 ? 3600000 : wait));

        if (bits & NOTIFY_SETTINGS) {
            settings_load(settings);
            next_stocks = 0;
        }
        if (bits & NOTIFY_REFRESH) {
            next_weather = 0;
            next_stocks = 0;
        }
        if (bits & NOTIFY_FLICKR) {
            next_flickr = 0;
        }
        if (bits & NOTIFY_OTA_CHECK) {
            next_ota = 0;
        }
        if (bits & NOTIFY_OTA_INSTALL) {
            ota_install();   // 성공하면 곧 재부팅, 네트워크가 없으면 바로 실패를 알린다
            log_stack("OTA install");
        }
        if (!wifi_mgr_is_connected()) {
            if (now_ms() >= next_flickr) {
                app_flickr_sync(false);   // 네트워크 없이: 삭제된 피드의 사진만 정리
            }
            // 연결되면 APP_EVT_WIFI_CONNECTED 로 깨어난다
            next_weather = next_stocks = next_flickr = next_ota = now_ms() + OFFLINE_WAIT_MS;
            continue;
        }

        if (now_ms() >= next_weather) {
            weather_info_t w;
            if (weather_fetch(&w) == ESP_OK) {
                app_state_set_weather(&w);
                next_weather = now_ms() + (int64_t)CONFIG_APP_WEATHER_REFRESH_MIN * 60 * 1000;
            } else {
                next_weather = now_ms() + WEATHER_RETRY_MS;
            }
        }
        if (now_ms() >= next_stocks) {
            if (stocks_fetch(settings->stock_symbols, stocks) == ESP_OK) {
                app_state_set_stocks(stocks);
                next_stocks = now_ms() + (int64_t)CONFIG_APP_STOCK_REFRESH_SEC * 1000;
            } else {
                next_stocks = now_ms() + STOCKS_RETRY_MS;
            }
        }
        if (now_ms() >= next_ota) {
            esp_err_t err = ota_check();   // 새 버전이면 APP_EVT_OTA_AVAILABLE
            next_ota = now_ms() + (err == ESP_OK || err == ESP_ERR_NOT_FOUND
                                   ? (int64_t)CONFIG_APP_OTA_CHECK_INTERVAL_H * 3600 * 1000 : OTA_RETRY_MS);
            log_stack("OTA check");
        }
        // 날씨/주가 다음에: 처음 동기화는 사진을 여러 장 받아 1분 넘게 걸릴 수 있다
        if (now_ms() >= next_flickr) {
            esp_err_t err = app_flickr_sync(true);
            next_flickr = now_ms() + (err == ESP_OK ? (int64_t)CONFIG_APP_FLICKR_SYNC_MIN * 60 * 1000 : FLICKR_RETRY_MS);
            log_stack("Flickr sync");
        }
    }
}

// cJSON 노드(작은 할당 다수)가 내부 RAM 을 차지하지 않도록 PSRAM 에 할당
static void *json_malloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : malloc(size);
}

esp_err_t net_worker_start(void)
{
    cJSON_Hooks hooks = { .malloc_fn = json_malloc, .free_fn = free };
    cJSON_InitHooks(&hooks);

    ESP_RETURN_ON_ERROR(esp_event_handler_register(APP_EVENT, ESP_EVENT_ANY_ID, on_app_event, NULL),
                        TAG, "event handler");
    BaseType_t ok = xTaskCreatePinnedToCore(worker, "net_worker", TASK_STACK, NULL, 5, &s_task, 0);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    if (wifi_mgr_is_connected()) {
        xTaskNotify(s_task, NOTIFY_REFRESH, eSetBits);
    }
    return ESP_OK;
}

void net_worker_request_refresh(void)
{
    if (s_task) {
        xTaskNotify(s_task, NOTIFY_REFRESH, eSetBits);
    }
}
