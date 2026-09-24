// 현재 날씨
//   - OpenWeatherMap (API 키가 설정된 경우): 도시 ID 또는 "이름,국가" 로 조회, 도시 이름도 받아 온다
//     https://openweathermap.org/current  (units=metric)
//     OWM 날씨 코드(2xx~8xx)는 UI 아이콘 매핑이 쓰는 WMO 코드로 바꿔 넣는다.
//   - Open-Meteo (키 없음): Kconfig 좌표
//     https://open-meteo.com/en/docs  - current=temperature_2m,relative_humidity_2m,weather_code,is_day

#include "services.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "net.h"
#include "sdkconfig.h"

static const char *TAG = "weather";

static volatile weather_status_t s_status = WEATHER_STATUS_NONE;

weather_status_t weather_last_status(void)
{
    return s_status;
}

// OpenWeatherMap condition id → WMO weather code (page_weather.c 의 wmo_describe 와 맞춘다)
static int owm_to_wmo(int id)
{
    if (id >= 200 && id < 300) return 95;                   // 뇌우
    if (id >= 300 && id < 400) return 53;                   // 이슬비
    if (id == 500) return 61;
    if (id == 501) return 63;
    if (id >= 502 && id <= 504) return 65;
    if (id == 511) return 66;                               // 어는 비
    if (id >= 520 && id < 600) return 81;                   // 소나기
    if (id >= 611 && id <= 616) return 66;                  // 진눈깨비
    if (id >= 620 && id < 700) return 85;                   // 소낙눈
    if (id >= 600 && id < 700) return id == 600 ? 71 : id == 601 ? 73 : 75;
    if (id >= 700 && id < 800) return 45;                   // 안개, 연무, 황사 등
    if (id == 800) return 0;
    if (id == 801 || id == 802) return id - 800;
    return 3;                                               // 803, 804 흐림
}

// 쿼리 값 URL 인코딩 (도시 이름에 공백/한글이 있을 수 있음)
static void url_encode(const char *in, char *out, size_t size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)in; *p && n + 4 < size; p++) {
        if (isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == ',') {
            out[n++] = (char)*p;
        } else {
            out[n++] = '%';
            out[n++] = hex[*p >> 4];
            out[n++] = hex[*p & 15];
        }
    }
    out[n] = '\0';
}

static bool all_digits(const char *s)
{
    if (!*s) {
        return false;
    }
    for (; *s; s++) {
        if (!isdigit((unsigned char)*s)) {
            return false;
        }
    }
    return true;
}

static esp_err_t get_json(const char *url, const char *log_url, cJSON **out_root, int *out_status)
{
    char *body = NULL;
    size_t len = 0;
    *out_root = NULL;
    esp_err_t err = http_get_alloc(url, NULL, &body, &len, out_status);
    if (err != ESP_OK) {
        s_status = WEATHER_STATUS_NET_ERROR;
        return err;
    }
    if (*out_status == 200) {
        *out_root = cJSON_Parse(body);
    } else {
        ESP_LOGW(TAG, "%s: HTTP %d %.120s", log_url, *out_status, body ? body : "");
    }
    free(body);
    return ESP_OK;
}

static esp_err_t fetch_owm(const char *api_key, const char *city, weather_info_t *out)
{
    char query[200];
    if (all_digits(city)) {
        snprintf(query, sizeof(query), "id=%s", city);
    } else if (city[0]) {
        char enc[180];
        url_encode(city, enc, sizeof(enc));
        snprintf(query, sizeof(query), "q=%s", enc);
    } else {
        snprintf(query, sizeof(query), "lat=%s&lon=%s", CONFIG_APP_WEATHER_LAT, CONFIG_APP_WEATHER_LON);
    }
    char url[320];
    snprintf(url, sizeof(url), "https://api.openweathermap.org/data/2.5/weather?%s&units=metric&appid=%s",
             query, api_key);

    cJSON *root = NULL;
    int status = 0;
    ESP_RETURN_ON_ERROR(get_json(url, "OpenWeatherMap", &root, &status), TAG, "request");
    if (status != 200) {
        s_status = status == 401 ? WEATHER_STATUS_BAD_KEY
                 : status == 404 ? WEATHER_STATUS_CITY_NOT_FOUND : WEATHER_STATUS_HTTP_ERROR;
        return ESP_FAIL;
    }

    cJSON *main_obj = cJSON_GetObjectItem(root, "main");
    cJSON *temp = cJSON_GetObjectItem(main_obj, "temp");
    cJSON *hum = cJSON_GetObjectItem(main_obj, "humidity");
    cJSON *cond = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "weather"), 0);
    cJSON *id = cJSON_GetObjectItem(cond, "id");
    const char *icon = cJSON_GetStringValue(cJSON_GetObjectItem(cond, "icon"));   // "01d" / "01n"
    const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(root, "name"));
    esp_err_t err = ESP_FAIL;
    if (cJSON_IsNumber(temp) && cJSON_IsNumber(hum) && cJSON_IsNumber(id)) {
        *out = (weather_info_t){
            .valid = true,
            .temp_c = (float)temp->valuedouble,
            .humidity = hum->valueint,
            .wmo_code = owm_to_wmo(id->valueint),
            .is_day = !(icon && icon[0] && icon[strlen(icon) - 1] == 'n'),
            .updated_at = time(NULL),
        };
        strlcpy(out->city, name ? name : "", sizeof(out->city));
        ESP_LOGI(TAG, "OWM %s: %d.%d C, %d%%, id %d -> wmo %d", out->city, (int)out->temp_c,
                 abs((int)(out->temp_c * 10) % 10), out->humidity, id->valueint, out->wmo_code);
        s_status = WEATHER_STATUS_OK;
        err = ESP_OK;
    } else {
        ESP_LOGW(TAG, "OWM: unexpected response");
        s_status = WEATHER_STATUS_PARSE_ERROR;
    }
    cJSON_Delete(root);
    return err;
}

static esp_err_t fetch_open_meteo(weather_info_t *out)
{
    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
             "&current=temperature_2m,relative_humidity_2m,weather_code,is_day&timezone=auto",
             CONFIG_APP_WEATHER_LAT, CONFIG_APP_WEATHER_LON);

    cJSON *root = NULL;
    int status = 0;
    ESP_RETURN_ON_ERROR(get_json(url, "Open-Meteo", &root, &status), TAG, "request");
    if (status != 200) {
        s_status = WEATHER_STATUS_HTTP_ERROR;
        return ESP_FAIL;
    }

    esp_err_t err = ESP_FAIL;
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
        s_status = WEATHER_STATUS_OK;
        err = ESP_OK;
    } else {
        ESP_LOGW(TAG, "Open-Meteo: unexpected response");
        s_status = WEATHER_STATUS_PARSE_ERROR;
    }
    cJSON_Delete(root);
    return err;
}

esp_err_t weather_fetch(const char *owm_api_key, const char *owm_city, weather_info_t *out)
{
    if (owm_api_key && owm_api_key[0]) {
        return fetch_owm(owm_api_key, owm_city ? owm_city : "", out);
    }
    return fetch_open_meteo(out);
}
