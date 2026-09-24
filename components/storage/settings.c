// 사용자 설정: NVS namespace "settings" 에 항목별 key 로 저장한다.
// 없는 항목은 menuconfig(Kconfig) 기본값을 쓰므로, 항목이 추가되어도 기존 저장값과 호환된다.
// 주의: NVS 암호화를 켜지 않았으므로 Wi-Fi 비밀번호는 flash 에 평문으로 저장된다.

#include "app_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_check.h"
#include "esp_random.h"
#include "nvs.h"
#include "sdkconfig.h"

#define NS              "settings"
#define KEY_SSID        "ssid"
#define KEY_PASS        "pass"
#define KEY_UI_MODE     "ui_mode"
#define KEY_ROTATION    "rotation"
#define KEY_LANGUAGE    "lang"
#define KEY_PHOTO_INT   "photo_int"
#define KEY_SYMBOLS     "symbols"
#define KEY_OWM_KEY     "owm_key"
#define KEY_W_CITY      "w_city"
#define KEY_WEB_PIN     "web_pin"
#define KEY_FEEDS       "feeds"
#define KEY_FEEDS_OLD   "flickr"    // v0.2.2 까지의 키 → 처음 읽을 때 KEY_FEEDS 로 옮김

static const char *TAG = "settings";

static void set_defaults(app_settings_t *s)
{
    memset(s, 0, sizeof(*s));
    strlcpy(s->wifi_ssid, CONFIG_APP_WIFI_SSID, sizeof(s->wifi_ssid));
    strlcpy(s->wifi_pass, CONFIG_APP_WIFI_PASSWORD, sizeof(s->wifi_pass));
    s->ui_mode = UI_MODE_WIDGET;
    s->rotation = UI_ROTATION_0;
    s->language = APP_LANG_KO;
    s->photo_interval_s = CONFIG_APP_PHOTO_INTERVAL_SEC;
    strlcpy(s->stock_symbols, CONFIG_APP_STOCK_SYMBOLS, sizeof(s->stock_symbols));
    strlcpy(s->owm_api_key, CONFIG_APP_OWM_API_KEY, sizeof(s->owm_api_key));
    strlcpy(s->weather_city, CONFIG_APP_OWM_CITY, sizeof(s->weather_city));
}

static void get_str(nvs_handle_t h, const char *key, char *out, size_t size)
{
    size_t len = size;
    if (nvs_get_str(h, key, out, &len) != ESP_OK) {
        return;   // 없거나 길이 초과: 기본값 유지
    }
}

esp_err_t settings_load(app_settings_t *out)
{
    set_defaults(out);

    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;   // 아직 저장한 적 없음
    }
    ESP_RETURN_ON_ERROR(err, TAG, "open");

    get_str(h, KEY_SSID, out->wifi_ssid, sizeof(out->wifi_ssid));
    get_str(h, KEY_PASS, out->wifi_pass, sizeof(out->wifi_pass));
    get_str(h, KEY_SYMBOLS, out->stock_symbols, sizeof(out->stock_symbols));
    get_str(h, KEY_OWM_KEY, out->owm_api_key, sizeof(out->owm_api_key));
    get_str(h, KEY_W_CITY, out->weather_city, sizeof(out->weather_city));

    uint8_t u8;
    if (nvs_get_u8(h, KEY_UI_MODE, &u8) == ESP_OK && u8 <= UI_MODE_SLIDE) {
        out->ui_mode = (ui_mode_t)u8;
    }
    if (nvs_get_u8(h, KEY_ROTATION, &u8) == ESP_OK && u8 <= UI_ROTATION_270) {
        out->rotation = (ui_rotation_t)u8;
    }
    if (nvs_get_u8(h, KEY_LANGUAGE, &u8) == ESP_OK && u8 <= APP_LANG_KO) {
        out->language = (app_lang_t)u8;
    }
    int32_t i32;
    if (nvs_get_i32(h, KEY_PHOTO_INT, &i32) == ESP_OK && i32 >= 3 && i32 <= 3600) {
        out->photo_interval_s = i32;
    }
    nvs_close(h);
    return ESP_OK;
}

esp_err_t settings_save(const app_settings_t *in)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &h), TAG, "open");

    esp_err_t err = ESP_OK;
    if (err == ESP_OK) err = nvs_set_str(h, KEY_SSID, in->wifi_ssid);
    if (err == ESP_OK) err = nvs_set_str(h, KEY_PASS, in->wifi_pass);
    if (err == ESP_OK) err = nvs_set_str(h, KEY_SYMBOLS, in->stock_symbols);
    if (err == ESP_OK) err = nvs_set_str(h, KEY_OWM_KEY, in->owm_api_key);
    if (err == ESP_OK) err = nvs_set_str(h, KEY_W_CITY, in->weather_city);
    if (err == ESP_OK) err = nvs_set_u8(h, KEY_UI_MODE, (uint8_t)in->ui_mode);
    if (err == ESP_OK) err = nvs_set_u8(h, KEY_ROTATION, (uint8_t)in->rotation);
    if (err == ESP_OK) err = nvs_set_u8(h, KEY_LANGUAGE, (uint8_t)in->language);
    if (err == ESP_OK) err = nvs_set_i32(h, KEY_PHOTO_INT, in->photo_interval_s);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    ESP_RETURN_ON_ERROR(err, TAG, "save");
    ESP_LOGI(TAG, "saved (ui_mode=%d rotation=%d lang=%d photo=%ds)", in->ui_mode, in->rotation,
             in->language, in->photo_interval_s);
    return ESP_OK;
}

// 예전 키("flickr")에 저장된 목록을 새 키로 옮긴다 (OTA 후 처음 한 번)
static void migrate_feeds_key(void)
{
    static bool s_done;
    if (s_done) {
        return;
    }
    s_done = true;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    size_t len = 0;
    if (nvs_get_str(h, KEY_FEEDS, NULL, &len) == ESP_ERR_NVS_NOT_FOUND &&
        nvs_get_str(h, KEY_FEEDS_OLD, NULL, &len) == ESP_OK) {
        char *buf = malloc(len);
        if (buf && nvs_get_str(h, KEY_FEEDS_OLD, buf, &len) == ESP_OK &&
            nvs_set_str(h, KEY_FEEDS, buf) == ESP_OK) {
            nvs_erase_key(h, KEY_FEEDS_OLD);
            nvs_commit(h);
            ESP_LOGI(TAG, "photo feed list moved to key \"%s\"", KEY_FEEDS);
        }
        free(buf);
    }
    nvs_close(h);
}

esp_err_t settings_get_photo_feeds(char *out, size_t size)
{
    migrate_feeds_key();
    out[0] = '\0';
    nvs_handle_t h;
    esp_err_t err = nvs_open(NS, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(err, TAG, "open");
    size_t len = size;
    err = nvs_get_str(h, KEY_FEEDS, out, &len);
    nvs_close(h);
    if (err != ESP_OK) {
        out[0] = '\0';
    }
    return err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

esp_err_t settings_set_photo_feeds(const char *feeds)
{
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &h), TAG, "open");
    esp_err_t err = nvs_set_str(h, KEY_FEEDS, feeds);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

esp_err_t settings_get_web_pin(char *out, size_t size)
{
    ESP_RETURN_ON_FALSE(size >= 7, ESP_ERR_INVALID_SIZE, TAG, "pin buffer");
    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &h), TAG, "open");

    size_t len = size;
    esp_err_t err = nvs_get_str(h, KEY_WEB_PIN, out, &len);
    if (err != ESP_OK || strlen(out) != 6) {
        snprintf(out, size, "%06u", (unsigned)(esp_random() % 1000000));
        err = nvs_set_str(h, KEY_WEB_PIN, out);
        if (err == ESP_OK) err = nvs_commit(h);
    }
    nvs_close(h);
    return err;
}
