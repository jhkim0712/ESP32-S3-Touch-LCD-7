// 현재 날씨: Open-Meteo (API 키 불필요)
// https://open-meteo.com/en/docs  - current=temperature_2m,relative_humidity_2m,weather_code,is_day

#include "services.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "net.h"
#include "sdkconfig.h"

static const char *TAG = "weather";

esp_err_t weather_fetch(weather_info_t *out)
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
             "&current=temperature_2m,relative_humidity_2m,weather_code,is_day&timezone=auto",
             CONFIG_APP_WEATHER_LAT, CONFIG_APP_WEATHER_LON);

    char *body = NULL;
    size_t len = 0;
    int status = 0;
    ESP_RETURN_ON_ERROR(http_get_alloc(url, NULL, &body, &len, &status), TAG, "request");
    if (status != 200) {
        ESP_LOGW(TAG, "HTTP %d", status);
        free(body);
        return ESP_FAIL;
    }

    esp_err_t err = ESP_FAIL;
    cJSON *root = cJSON_Parse(body);
    cJSON *cur = cJSON_GetObjectItem(root, "current");
    cJSON *temp = cJSON_GetObjectItem(cur, "temperature_2m");
    cJSON *hum = cJSON_GetObjectItem(cur, "relative_humidity_2m");
    cJSON *code = cJSON_GetObjectItem(cur, "weather_code");
    cJSON *day = cJSON_GetObjectItem(cur, "is_day");
    if (cJSON_IsNumber(temp) && cJSON_IsNumber(hum) && cJSON_IsNumber(code)) {
        *out = (weather_info_t){
            .valid = true,
            .temp_c = (float)temp->valuedouble,
            .humidity = hum->valueint,
            .wmo_code = code->valueint,
            .is_day = cJSON_IsNumber(day) ? day->valueint != 0 : true,
            .updated_at = time(NULL),
        };
        ESP_LOGI(TAG, "%s: %d.%d C, %d%%, code %d", CONFIG_APP_WEATHER_CITY,
                 (int)out->temp_c, abs((int)(out->temp_c * 10) % 10), out->humidity, out->wmo_code);
        err = ESP_OK;
    } else {
        ESP_LOGW(TAG, "unexpected response: %.120s", body);
    }
    cJSON_Delete(root);
    free(body);
    return err;
}
