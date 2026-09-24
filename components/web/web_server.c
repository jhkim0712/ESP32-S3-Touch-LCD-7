// 웹 설정 서버 (esp_http_server + mDNS). API 는 web_server.h 참고.
// 핸들러는 모두 HTTP 서버 태스크 하나에서 순서대로 실행되므로 PIN 실패 횟수 등에 잠금이 필요 없다.

#include "web_server.h"
#include "web_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "app_state.h"
#include "app_storage.h"
#include "board.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "mdns.h"
#include "net.h"
#include "services.h"

#define MAX_BODY_LEN        4096   // 사진 피드 목록 (8 x 256)
#define PIN_MAX_FAILS       5
#define PIN_LOCK_US         (30 * 1000 * 1000LL)
#define SCAN_TIMEOUT_MS     10000
#define SCAN_DONE_BIT       BIT0

extern const uint8_t index_html_gz_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_gz_end[] asm("_binary_index_html_gz_end");

static const char *TAG = "web";

static httpd_handle_t      s_server;
static EventGroupHandle_t  s_scan_events;
static esp_timer_handle_t  s_restart_timer;
static char                s_pin[8];
static int                 s_pin_fails;
static int64_t             s_locked_until_us;
static ui_rotation_t       s_boot_rotation;   // 화면에 적용된 회전 (바꾸면 재부팅 필요)

// ---- 공통 ----

esp_err_t web_send_error(httpd_req_t *req, const char *status, const char *message)
{
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    char body[96];
    snprintf(body, sizeof(body), "{\"error\":\"%s\"}", message);
    return httpd_resp_sendstr(req, body);
}

esp_err_t web_send_json(httpd_req_t *req, cJSON *root)
{
    char *text = root ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    if (!text) {
        return web_send_error(req, "500 Internal Server Error", "no memory");
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_sendstr(req, text);
    free(text);
    return err;
}

bool web_authorized(httpd_req_t *req)
{
    int64_t now = esp_timer_get_time();
    if (now < s_locked_until_us) {
        web_send_error(req, "429 Too Many Requests", "locked");
        return false;
    }
    char pin[8] = "";
    if (httpd_req_get_hdr_value_str(req, "X-PIN", pin, sizeof(pin)) != ESP_OK) {
        web_query_param(req, "pin", pin, sizeof(pin));   // 헤더를 붙일 수 없는 링크용 (?pin=)
    }
    if (pin[0] && strcmp(pin, s_pin) == 0) {
        s_pin_fails = 0;
        return true;
    }
    if (++s_pin_fails >= PIN_MAX_FAILS) {
        ESP_LOGW(TAG, "too many wrong PINs, locked for 30 s");
        s_pin_fails = 0;
        s_locked_until_us = now + PIN_LOCK_US;
    }
    web_send_error(req, "401 Unauthorized", "pin");
    return false;
}

bool web_query_param(httpd_req_t *req, const char *key, char *out, size_t size)
{
    out[0] = '\0';
    size_t len = httpd_req_get_url_query_len(req);
    char *query = len ? malloc(len + 1) : NULL;
    bool ok = query && httpd_req_get_url_query_str(req, query, len + 1) == ESP_OK &&
              httpd_query_key_value(query, key, out, size) == ESP_OK;
    free(query);
    if (!ok) {
        out[0] = '\0';
        return false;
    }
    // URL 디코딩 (제자리): "+" → 공백, "%XX" → 바이트
    char *w = out;
    for (const char *p = out; *p; p++) {
        if (*p == '+') {
            *w++ = ' ';
        } else if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
            char hex[3] = { p[1], p[2], '\0' };
            *w++ = (char)strtol(hex, NULL, 16);
            p += 2;
        } else {
            *w++ = *p;
        }
    }
    *w = '\0';
    return true;
}

// 요청 본문을 JSON 으로 읽는다. 실패하면 오류 응답을 보내고 NULL 을 반환한다.
cJSON *web_read_json_body(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > MAX_BODY_LEN) {
        web_send_error(req, "400 Bad Request", "body size");
        return NULL;
    }
    char *buf = malloc(req->content_len + 1);
    if (!buf) {
        web_send_error(req, "500 Internal Server Error", "no memory");
        return NULL;
    }
    size_t got = 0;
    while (got < req->content_len) {
        int n = httpd_req_recv(req, buf + got, req->content_len - got);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            free(buf);
            return NULL;   // 연결 끊김: 응답할 수 없음
        }
        got += n;
    }
    buf[got] = '\0';
    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        web_send_error(req, "400 Bad Request", "invalid json");
        return NULL;
    }
    return root;
}

// ---- 페이지 ----

static esp_err_t get_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)index_html_gz_start, index_html_gz_end - index_html_gz_start);
}

// ---- API ----

static esp_err_t get_status(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return ESP_OK;
    }
    const esp_app_desc_t *app = esp_app_get_description();
    app_settings_t s;
    settings_load(&s);
    char ip[16];
    app_state_get_ip(ip, sizeof(ip));

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version", app->version);
    cJSON_AddStringToObject(root, "idf", app->idf_ver);
    cJSON_AddBoolToObject(root, "wifi_connected", wifi_mgr_is_connected());
    cJSON_AddStringToObject(root, "ip", ip);
    wifi_ap_record_t ap;
    if (wifi_mgr_is_connected() && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        cJSON_AddStringToObject(root, "ssid", (const char *)ap.ssid);
        cJSON_AddNumberToObject(root, "rssi", ap.rssi);
    }
    cJSON_AddBoolToObject(root, "time_synced", time_is_synced());
    cJSON_AddNumberToObject(root, "time", (double)time(NULL));
    cJSON_AddNumberToObject(root, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(root, "heap_internal_kb", heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
    cJSON_AddNumberToObject(root, "heap_psram_kb", heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
    cJSON_AddBoolToObject(root, "restart_required", s.rotation != s_boot_rotation);

    static const char *const k_weather_status[] = { "none", "ok", "network", "bad_key", "city_not_found", "http", "parse" };
    weather_status_t ws = weather_last_status();
    weather_info_t w;
    app_state_get_weather(&w);
    cJSON *weather = cJSON_AddObjectToObject(root, "weather");
    cJSON_AddStringToObject(weather, "provider", s.owm_api_key[0] ? "openweathermap" : "open-meteo");
    cJSON_AddStringToObject(weather, "status", ws < sizeof(k_weather_status) / sizeof(k_weather_status[0])
                                               ? k_weather_status[ws] : "unknown");
    if (w.valid) {
        cJSON_AddStringToObject(weather, "city", w.city);
        cJSON_AddNumberToObject(weather, "temp", w.temp_c);
        cJSON_AddNumberToObject(weather, "humidity", w.humidity);
        cJSON_AddNumberToObject(weather, "updated_at", (double)w.updated_at);
    }
    return web_send_json(req, root);
}

static esp_err_t get_settings(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return ESP_OK;
    }
    app_settings_t s;
    if (settings_load(&s) != ESP_OK) {
        return web_send_error(req, "500 Internal Server Error", "load failed");
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "wifi_ssid", s.wifi_ssid);
    cJSON_AddBoolToObject(root, "wifi_pass_set", s.wifi_pass[0] != '\0');
    cJSON_AddStringToObject(root, "ui_mode", s.ui_mode == UI_MODE_SLIDE ? "slide" : "widget");
    cJSON_AddStringToObject(root, "language", s.language == APP_LANG_KO ? "ko" : "en");
    cJSON_AddNumberToObject(root, "rotation", s.rotation * 90);
    cJSON_AddNumberToObject(root, "photo_interval_s", s.photo_interval_s);
    cJSON_AddStringToObject(root, "stock_symbols", s.stock_symbols);
    // API 키는 비밀번호처럼 돌려주지 않는다: 설정 여부와 끝 4자리만
    size_t key_len = strlen(s.owm_api_key);
    cJSON_AddBoolToObject(root, "owm_api_key_set", key_len > 0);
    cJSON_AddStringToObject(root, "owm_api_key_hint", key_len >= 4 ? s.owm_api_key + key_len - 4 : "");
    cJSON_AddStringToObject(root, "weather_city", s.weather_city);
    return web_send_json(req, root);
}

// 문자열 항목: 없으면 그대로 두고, 있으면 길이를 확인해 복사한다
static bool take_string(cJSON *root, const char *key, char *out, size_t size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!item) {
        return true;
    }
    if (!cJSON_IsString(item) || strlen(item->valuestring) >= size) {
        return false;
    }
    strlcpy(out, item->valuestring, size);
    return true;
}

static esp_err_t post_settings(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return ESP_OK;
    }
    cJSON *root = web_read_json_body(req);
    if (!root) {
        return ESP_OK;
    }
    app_settings_t s;
    settings_load(&s);   // 보내지 않은 항목은 유지
    char old_ssid[sizeof(s.wifi_ssid)], old_pass[sizeof(s.wifi_pass)];
    strlcpy(old_ssid, s.wifi_ssid, sizeof(old_ssid));
    strlcpy(old_pass, s.wifi_pass, sizeof(old_pass));

    const char *bad = NULL;
    if (!take_string(root, "wifi_ssid", s.wifi_ssid, sizeof(s.wifi_ssid))) bad = "wifi_ssid";
    if (!take_string(root, "wifi_pass", s.wifi_pass, sizeof(s.wifi_pass))) bad = "wifi_pass";
    if (!take_string(root, "stock_symbols", s.stock_symbols, sizeof(s.stock_symbols))) bad = "stock_symbols";
    if (!take_string(root, "owm_api_key", s.owm_api_key, sizeof(s.owm_api_key))) bad = "owm_api_key";
    if (!take_string(root, "weather_city", s.weather_city, sizeof(s.weather_city))) bad = "weather_city";
    for (const char *p = s.owm_api_key; *p && !bad; p++) {
        if (!isalnum((unsigned char)*p)) bad = "owm_api_key";   // URL 에 그대로 들어간다
    }
    for (const unsigned char *p = (const unsigned char *)s.weather_city; *p && !bad; p++) {
        if (*p < 0x20) bad = "weather_city";
    }

    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, "ui_mode");
    if (item) {
        const char *v = cJSON_GetStringValue(item);
        if (v && strcmp(v, "widget") == 0) s.ui_mode = UI_MODE_WIDGET;
        else if (v && strcmp(v, "slide") == 0) s.ui_mode = UI_MODE_SLIDE;
        else bad = "ui_mode";
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "language");
    if (item) {
        const char *v = cJSON_GetStringValue(item);
        if (v && strcmp(v, "en") == 0) s.language = APP_LANG_EN;
        else if (v && strcmp(v, "ko") == 0) s.language = APP_LANG_KO;
        else bad = "language";
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "rotation");
    if (item) {
        int deg = cJSON_IsNumber(item) ? item->valueint : -1;
        if (deg == 0 || deg == 90 || deg == 180 || deg == 270) s.rotation = (ui_rotation_t)(deg / 90);
        else bad = "rotation";
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "photo_interval_s");
    if (item) {
        int sec = cJSON_IsNumber(item) ? item->valueint : 0;
        if (sec >= 3 && sec <= 3600) s.photo_interval_s = sec;
        else bad = "photo_interval_s";
    }
    cJSON_Delete(root);

    if (bad) {
        return web_send_error(req, "400 Bad Request", bad);
    }
    if (settings_save(&s) != ESP_OK) {
        return web_send_error(req, "500 Internal Server Error", "save failed");
    }
    bool wifi_changed = strcmp(old_ssid, s.wifi_ssid) != 0 || strcmp(old_pass, s.wifi_pass) != 0;
    ESP_LOGI(TAG, "settings saved from web%s", wifi_changed ? " (Wi-Fi changed)" : "");

    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddBoolToObject(resp, "wifi_changed", wifi_changed);
    cJSON_AddBoolToObject(resp, "restart_required", s.rotation != s_boot_rotation);
    esp_err_t err = web_send_json(req, resp);

    // 응답을 보낸 뒤 알린다 (Wi-Fi 가 바뀌면 곧바로 재연결하면서 이 연결이 끊긴다)
    const app_settings_src_t src = APP_SETTINGS_SRC_WEB;
    app_event_post(APP_EVT_SETTINGS_CHANGED, &src, sizeof(src));
    return err;
}

static esp_err_t post_wifi_scan(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return ESP_OK;
    }
    xEventGroupClearBits(s_scan_events, SCAN_DONE_BIT);
    app_event_post(APP_EVT_REQ_WIFI_SCAN, NULL, 0);
    if (!(xEventGroupWaitBits(s_scan_events, SCAN_DONE_BIT, pdTRUE, pdTRUE, pdMS_TO_TICKS(SCAN_TIMEOUT_MS)) &
          SCAN_DONE_BIT)) {
        return web_send_error(req, "504 Gateway Timeout", "scan timeout");
    }

    wifi_scan_t *scan = malloc(sizeof(wifi_scan_t));
    if (!scan) {
        return web_send_error(req, "500 Internal Server Error", "no memory");
    }
    app_state_get_wifi_scan(scan);
    cJSON *root = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(root, "networks");
    for (int i = 0; i < scan->count; i++) {
        cJSON *ap = cJSON_CreateObject();
        cJSON_AddStringToObject(ap, "ssid", scan->aps[i].ssid);
        cJSON_AddNumberToObject(ap, "rssi", scan->aps[i].rssi);
        cJSON_AddBoolToObject(ap, "secure", scan->aps[i].secure);
        cJSON_AddItemToArray(list, ap);
    }
    free(scan);
    return web_send_json(req, root);
}

static void on_restart_timer(void *arg)
{
    board_backlight_set(false);   // 재부팅 중 노이즈 화면을 감춘다
    esp_restart();
}

static esp_err_t post_restart(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "restart requested from web");
    esp_err_t err = httpd_resp_sendstr(req, "{\"ok\":true}");
    esp_timer_start_once(s_restart_timer, 500 * 1000);   // 응답이 전송될 시간을 준다
    return err;
}

static void on_app_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == APP_EVT_WIFI_SCAN_DONE) {
        xEventGroupSetBits(s_scan_events, SCAN_DONE_BIT);
    }
}

// ---- 시작 ----

static void start_mdns(void)
{
    esp_err_t err = mdns_init();
    if (err == ESP_OK) {
        mdns_hostname_set(WEB_HOSTNAME);
        mdns_instance_name_set("Smart Display");
        err = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS: %s (use the IP address instead)", esp_err_to_name(err));
    }
}

esp_err_t web_server_start(void)
{
    ESP_RETURN_ON_ERROR(settings_get_web_pin(s_pin, sizeof(s_pin)), TAG, "pin");
    app_settings_t s;
    ESP_RETURN_ON_ERROR(settings_load(&s), TAG, "settings");
    s_boot_rotation = s.rotation;

    s_scan_events = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_scan_events, ESP_ERR_NO_MEM, TAG, "event group");
    const esp_timer_create_args_t timer_args = { .callback = on_restart_timer, .name = "web_restart" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_restart_timer), TAG, "timer");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(APP_EVENT, APP_EVT_WIFI_SCAN_DONE, on_app_event, NULL), TAG,
                        "app evt");

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.core_id = 0;
    cfg.stack_size = 6144;          // NVS/SD 쓰기가 있어 내부 RAM 스택 (PSRAM 스택 불가)
    cfg.max_open_sockets = 3;       // 소켓당 내부 RAM 을 쓰므로 작게. 넘치면 오래된 연결을 닫는다
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 20;
    ESP_RETURN_ON_ERROR(httpd_start(&s_server, &cfg), TAG, "httpd start");

    static const httpd_uri_t uris[] = {
        { .uri = "/",              .method = HTTP_GET,  .handler = get_index },
        { .uri = "/api/status",    .method = HTTP_GET,  .handler = get_status },
        { .uri = "/api/settings",  .method = HTTP_GET,  .handler = get_settings },
        { .uri = "/api/settings",  .method = HTTP_POST, .handler = post_settings },
        { .uri = "/api/wifi/scan", .method = HTTP_POST, .handler = post_wifi_scan },
        { .uri = "/api/restart",   .method = HTTP_POST, .handler = post_restart },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(s_server, &uris[i]), TAG, "uri %s", uris[i].uri);
    }
    ESP_RETURN_ON_ERROR(web_photos_register(s_server), TAG, "photos");
    ESP_RETURN_ON_ERROR(web_feeds_register(s_server), TAG, "feeds");
    ESP_RETURN_ON_ERROR(web_ota_register(s_server), TAG, "ota");

    start_mdns();
    ESP_LOGI(TAG, "web settings at http://%s.local (port 80)", WEB_HOSTNAME);
    return ESP_OK;
}
