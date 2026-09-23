// UI 확인용 가짜 데이터 공급기 (CONFIG_APP_UI_DEMO_DATA)
//   - 시각: 동기화 전이면 2026-09-23 21:30 KST 로 설정
//   - 날씨/주가/음악: 샘플 값, 주가는 5초마다 무작위 변동, 재생 시간은 1초마다 증가
//   - 음악 버튼: 재생/일시정지/이전/다음 동작을 흉내
//   - 30초 후 OTA 팝업 1회, "Update" 누르면 진행률만 흉내 (재부팅 안 함)

#include "demo_data.h"
#include "sdkconfig.h"

#if CONFIG_APP_UI_DEMO_DATA

#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "app_state.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "media_ble.h"
#include "ota.h"

static const char *TAG = "demo";

typedef struct {
    const char *title;
    const char *artist;
    const char *album;
    uint32_t    duration_s;
} demo_song_t;

static const demo_song_t s_songs[] = {
    { "Blinding Lights", "The Weeknd", "After Hours", 200 },
    { "Levitating", "Dua Lipa", "Future Nostalgia", 203 },
    { "As It Was", "Harry Styles", "Harry's House", 167 },
};

static media_info_t s_media;
static int          s_song;
static volatile bool s_ota_requested;

static void load_song(int index)
{
    s_song = (index + (int)(sizeof(s_songs) / sizeof(s_songs[0]))) % (int)(sizeof(s_songs) / sizeof(s_songs[0]));
    const demo_song_t *s = &s_songs[s_song];
    strlcpy(s_media.title, s->title, sizeof(s_media.title));
    strlcpy(s_media.artist, s->artist, sizeof(s_media.artist));
    strlcpy(s_media.album, s->album, sizeof(s_media.album));
    s_media.duration_s = s->duration_s;
    s_media.elapsed_s = 0;
}

static void on_request(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == APP_EVT_REQ_MEDIA_CMD) {
        media_cmd_t cmd = *(const media_cmd_t *)data;
        switch (cmd) {
        case MEDIA_CMD_PLAY_PAUSE:
            s_media.state = s_media.state == MEDIA_PLAYING ? MEDIA_PAUSED : MEDIA_PLAYING;
            break;
        case MEDIA_CMD_NEXT: load_song(s_song + 1); break;
        case MEDIA_CMD_PREV: load_song(s_song - 1); break;
        default: break;
        }
        ESP_LOGI(TAG, "media cmd %d", cmd);
        app_state_set_media(&s_media);
    } else if (id == APP_EVT_REQ_OTA_START) {
        s_ota_requested = true;
    } else if (id == APP_EVT_REQ_REFRESH) {
        ESP_LOGI(TAG, "refresh requested");
    }
}

static void set_demo_time(void)
{
    if (time(NULL) > 1735689600) {   // 2025-01-01 이후면 이미 동기화된 것
        return;
    }
    struct tm tm = { .tm_year = 2026 - 1900, .tm_mon = 8, .tm_mday = 23, .tm_hour = 21, .tm_min = 30 };
    struct timeval tv = { .tv_sec = mktime(&tm) };   // TZ(KST) 기준 해석
    settimeofday(&tv, NULL);
    app_event_post(APP_EVT_TIME_SYNCED, NULL, 0);
}

static void demo_task(void *arg)
{
    set_demo_time();
    app_event_post(APP_EVT_WIFI_CONNECTED, NULL, 0);
    ble_state_t ble = BLE_STATE_CONNECTED;
    app_event_post(APP_EVT_BLE_STATE, &ble, sizeof(ble));

    weather_info_t w = {
        .valid = true, .temp_c = 22.4f, .humidity = 58, .wmo_code = 2, .is_day = false,
        .updated_at = time(NULL),
    };
    app_state_set_weather(&w);

    stock_list_t st = {
        .count = 4,
        .items = {
            { .symbol = "AAPL", .name = "Apple", .price = 228.35, .change_pct = 1.23, .currency = "USD" },
            { .symbol = "NVDA", .name = "NVIDIA", .price = 131.20, .change_pct = -2.05, .currency = "USD" },
            { .symbol = "^KS11", .name = "KOSPI", .price = 2615.42, .change_pct = 0.48, .currency = "KRW" },
            { .symbol = "005930.KS", .name = "Samsung Elec", .price = 71500, .change_pct = -0.83, .currency = "KRW" },
        },
        .updated_at = time(NULL),
    };
    app_state_set_stocks(&st);

    load_song(0);
    s_media.state = MEDIA_PLAYING;
    app_state_set_media(&s_media);

    for (int tick = 1;; tick++) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (s_media.state == MEDIA_PLAYING) {
            if (++s_media.elapsed_s >= s_media.duration_s) {
                load_song(s_song + 1);
            }
            app_state_set_media(&s_media);
        }

        if (tick % 5 == 0) {
            for (int i = 0; i < st.count; i++) {
                double step = ((int)(esp_random() % 21) - 10) / 1000.0;   // ±1%
                st.items[i].price *= 1.0 + step / 10.0;
                st.items[i].change_pct += step * 10.0;
            }
            st.updated_at = time(NULL);
            app_state_set_stocks(&st);
        }

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
