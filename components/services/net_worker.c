// 네트워크 데이터 수집 태스크 (core 0)
// HTTPS 요청을 이 태스크 하나에서 순서대로 처리해 TLS 세션이 동시에 하나만 열리게 한다
// (TLS 세션 하나가 내부 RAM 을 수십 KB 사용).
//   - 날씨: CONFIG_APP_WEATHER_REFRESH_MIN 마다, 실패 시 1분 후 재시도
//   - 주가: CONFIG_APP_STOCK_REFRESH_SEC 마다, 실패 시 30초 후 재시도
//   - Wi-Fi 연결 직후, 새로고침 요청, 설정 변경(종목) 시 즉시 갱신

#include "services.h"

#include <stdlib.h>
#include "app_storage.h"
#include "cJSON.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net.h"
#include "sdkconfig.h"

#define NOTIFY_REFRESH      BIT0
#define NOTIFY_SETTINGS     BIT1
#define WEATHER_RETRY_MS    (60 * 1000)
#define STOCKS_RETRY_MS     (30 * 1000)
#define TASK_STACK          (10 * 1024)   // TLS 핸드셰이크(ECDHE) 여유 포함

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
    case APP_EVT_REQ_REFRESH:
        xTaskNotify(s_task, NOTIFY_REFRESH, eSetBits);
        break;
    case APP_EVT_SETTINGS_CHANGED:
        xTaskNotify(s_task, NOTIFY_SETTINGS, eSetBits);
        break;
    default:
        break;
    }
}

static void worker(void *arg)
{
    app_settings_t *settings = calloc(1, sizeof(app_settings_t));
    stock_list_t *stocks = calloc(1, sizeof(stock_list_t));
    configASSERT(settings && stocks);
    settings_load(settings);

    int64_t next_weather = 0, next_stocks = 0;
    for (;;) {
        int64_t next = next_weather < next_stocks ? next_weather : next_stocks;
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
        if (!wifi_mgr_is_connected()) {
            // 연결되면 APP_EVT_WIFI_CONNECTED 로 깨어난다
            next_weather = next_stocks = now_ms() + 3600000;
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
        ESP_LOGD(TAG, "stack high water mark: %u bytes", (unsigned)uxTaskGetStackHighWaterMark(NULL));
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
