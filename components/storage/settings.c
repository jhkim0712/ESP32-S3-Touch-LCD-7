// 사용자 설정: NVS namespace "settings" 에 항목별 key 로 저장한다.
// 없는 항목은 menuconfig(Kconfig) 기본값을 쓰므로, 항목이 추가되어도 기존 저장값과 호환된다.
// 주의: NVS 암호화를 켜지 않았으므로 Wi-Fi 비밀번호는 flash 에 평문으로 저장된다.

#include "app_storage.h"

#include <string.h>
#include "esp_check.h"
#include "nvs.h"
#include "sdkconfig.h"

#define NS              "settings"
#define KEY_SSID        "ssid"
#define KEY_PASS        "pass"
#define KEY_UI_MODE     "ui_mode"
#define KEY_ROTATION    "rotation"
#define KEY_PHOTO_INT   "photo_int"
#define KEY_SYMBOLS     "symbols"

static const char *TAG = "settings";

static void set_defaults(app_settings_t *s)
{
    memset(s, 0, sizeof(*s));
    strlcpy(s->wifi_ssid, CONFIG_APP_WIFI_SSID, sizeof(s->wifi_ssid));
    strlcpy(s->wifi_pass, CONFIG_APP_WIFI_PASSWORD, sizeof(s->wifi_pass));
    s->ui_mode = UI_MODE_WIDGET;
    s->rotation = UI_ROTATION_0;
    s->photo_interval_s = CONFIG_APP_PHOTO_INTERVAL_SEC;
    strlcpy(s->stock_symbols, CONFIG_APP_STOCK_SYMBOLS, sizeof(s->stock_symbols));
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

    uint8_t u8;
    if (nvs_get_u8(h, KEY_UI_MODE, &u8) == ESP_OK && u8 <= UI_MODE_SLIDE) {
        out->ui_mode = (ui_mode_t)u8;
    }
    if (nvs_get_u8(h, KEY_ROTATION, &u8) == ESP_OK && u8 <= UI_ROTATION_270) {
        out->rotation = (ui_rotation_t)u8;
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
    if (err == ESP_OK) err = nvs_set_u8(h, KEY_UI_MODE, (uint8_t)in->ui_mode);
    if (err == ESP_OK) err = nvs_set_u8(h, KEY_ROTATION, (uint8_t)in->rotation);
    if (err == ESP_OK) err = nvs_set_i32(h, KEY_PHOTO_INT, in->photo_interval_s);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);

    ESP_RETURN_ON_ERROR(err, TAG, "save");
    ESP_LOGI(TAG, "saved (ui_mode=%d rotation=%d photo=%ds)", in->ui_mode, in->rotation, in->photo_interval_s);
    return ESP_OK;
}
