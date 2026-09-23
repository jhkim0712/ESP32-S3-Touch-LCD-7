#include "app_state.h"

#include <string.h>
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

ESP_EVENT_DEFINE_BASE(APP_EVENT);

static const char *TAG = "app_state";

static SemaphoreHandle_t s_lock;
static weather_info_t    s_weather;
static stock_list_t      s_stocks;
static media_info_t      s_media;

esp_err_t app_state_init(void)
{
    if (s_lock) {
        return ESP_OK;
    }
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");

    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_ERR_INVALID_STATE) {   // 이미 만들어져 있으면 그대로 사용
        ESP_RETURN_ON_ERROR(err, TAG, "default event loop");
    }
    return ESP_OK;
}

esp_err_t app_event_post(app_event_id_t id, const void *data, size_t size)
{
    esp_err_t err = esp_event_post(APP_EVENT, id, data, size, 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "post event %d failed: %s", id, esp_err_to_name(err));
    }
    return err;
}

#define STATE_SET(dst, src, evt)             \
    do {                                     \
        xSemaphoreTake(s_lock, portMAX_DELAY); \
        memcpy(&(dst), (src), sizeof(dst));  \
        xSemaphoreGive(s_lock);              \
        app_event_post((evt), NULL, 0);      \
    } while (0)

#define STATE_GET(out, src)                  \
    do {                                     \
        xSemaphoreTake(s_lock, portMAX_DELAY); \
        memcpy((out), &(src), sizeof(src));  \
        xSemaphoreGive(s_lock);              \
    } while (0)

void app_state_set_weather(const weather_info_t *w) { STATE_SET(s_weather, w, APP_EVT_WEATHER_UPDATED); }
void app_state_get_weather(weather_info_t *out)     { STATE_GET(out, s_weather); }

void app_state_set_stocks(const stock_list_t *s)    { STATE_SET(s_stocks, s, APP_EVT_STOCKS_UPDATED); }
void app_state_get_stocks(stock_list_t *out)        { STATE_GET(out, s_stocks); }

void app_state_set_media(const media_info_t *m)     { STATE_SET(s_media, m, APP_EVT_MEDIA_UPDATED); }
void app_state_get_media(media_info_t *out)         { STATE_GET(out, s_media); }
