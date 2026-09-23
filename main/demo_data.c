// UI 확인용 가짜 데이터 공급기 (CONFIG_APP_UI_DEMO_DATA)
//   (시각/Wi-Fi/날씨/주가/음악은 실제 서비스가 담당하므로 흉내 내지 않는다)
//   - 30초 후 OTA 팝업 1회, "Update" 누르면 진행률만 흉내 (재부팅 안 함)

#include "demo_data.h"
#include "sdkconfig.h"

#if CONFIG_APP_UI_DEMO_DATA

#include <string.h>
#include <time.h>
#include "app_state.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ota.h"

static const char *TAG = "demo";

static volatile bool s_ota_requested;

static void on_request(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == APP_EVT_REQ_OTA_START) {
        s_ota_requested = true;
    }
}

static void demo_task(void *arg)
{
    for (int tick = 1;; tick++) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (tick == 30) {
            ota_release_t rel = { .tag = "v0.2.0", .asset_size = 1200000 };
            strlcpy(rel.notes, "Demo release notes:\n- Widget and slide UI\n- Faster photo loading", sizeof(rel.notes));
            app_event_post(APP_EVT_OTA_AVAILABLE, &rel, sizeof(rel));
        }

        if (s_ota_requested) {
            s_ota_requested = false;
            for (int p = 0; p <= 100; p += 5) {
                app_event_post(APP_EVT_OTA_PROGRESS, &p, sizeof(p));
                vTaskDelay(pdMS_TO_TICKS(150));
            }
            esp_err_t done = ESP_OK;
            app_event_post(APP_EVT_OTA_DONE, &done, sizeof(done));
        }
    }
}

void demo_data_start(void)
{
    ESP_LOGW(TAG, "UI demo data enabled (CONFIG_APP_UI_DEMO_DATA)");
    esp_event_handler_register(APP_EVENT, ESP_EVENT_ANY_ID, on_request, NULL);
    xTaskCreatePinnedToCore(demo_task, "demo", 4096, NULL, 3, NULL, 0);
}

#else

void demo_data_start(void) {}

#endif
