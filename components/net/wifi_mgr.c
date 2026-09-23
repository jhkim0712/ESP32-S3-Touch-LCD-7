// Wi-Fi STA 관리
//   - 연결이 끊기면 1s → 2s → ... → 30s 간격으로 재시도
//   - APP_EVT_REQ_WIFI_SCAN: 재연결 시도를 멈추고 스캔 → 결과를 app_state 에 기록
//   - APP_EVT_SETTINGS_CHANGED: SSID/비밀번호가 바뀌었으면 새 설정으로 다시 연결
// 자격 증명은 우리 NVS 설정(settings)에만 저장하고, Wi-Fi 드라이버 저장소는 RAM 으로 둔다.
// 모든 핸들러는 기본 이벤트 루프 태스크에서, 재시도는 esp_timer 태스크에서 실행된다.

#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_state.h"
#include "app_storage.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define CONNECTED_BIT       BIT0
#define BACKOFF_MIN_MS      1000
#define BACKOFF_MAX_MS      30000
#define SCAN_MAX_RECORDS    40

static const char *TAG = "wifi";

static EventGroupHandle_t s_events;
static esp_timer_handle_t s_retry_timer;
static esp_netif_t       *s_netif;
static char               s_ssid[33];
static char               s_pass[65];
static int                s_backoff_ms = BACKOFF_MIN_MS;
static volatile bool      s_scanning;

static bool have_credentials(void)
{
    return s_ssid[0] != '\0';
}

static void apply_config(void)
{
    wifi_config_t cfg = { 0 };
    strlcpy((char *)cfg.sta.ssid, s_ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, s_pass, sizeof(cfg.sta.password));
    cfg.sta.threshold.authmode = s_pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;     // WPA3 공유기도 허용
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_STA, &cfg));
}

static void connect_now(void)
{
    if (!have_credentials() || s_scanning) {
        return;
    }
    ESP_LOGI(TAG, "connecting to \"%s\"", s_ssid);
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "connect: %s", esp_err_to_name(err));
    }
}

static void schedule_retry(int delay_ms)
{
    esp_timer_stop(s_retry_timer);   // 실행 중이 아니면 에러를 반환하지만 무시
    esp_timer_start_once(s_retry_timer, (uint64_t)delay_ms * 1000);
}

static void on_retry_timer(void *arg)
{
    connect_now();
}

static void publish_scan_results(void)
{
    uint16_t n = SCAN_MAX_RECORDS;
    wifi_ap_record_t *recs = calloc(n, sizeof(wifi_ap_record_t));
    wifi_scan_t *out = calloc(1, sizeof(wifi_scan_t));
    if (recs && out && esp_wifi_scan_get_ap_records(&n, recs) == ESP_OK) {
        // 드라이버가 신호 세기 순으로 정렬해 준다. 같은 SSID(메시/중계기)는 가장 강한 것만 남긴다.
        for (int i = 0; i < n && out->count < APP_MAX_WIFI_AP; i++) {
            const char *ssid = (const char *)recs[i].ssid;
            if (ssid[0] == '\0') {
                continue;   // 숨김 SSID
            }
            bool dup = false;
            for (int j = 0; j < out->count; j++) {
                if (strcmp(out->aps[j].ssid, ssid) == 0) {
                    dup = true;
                    break;
                }
            }
            if (dup) {
                continue;
            }
            wifi_ap_t *ap = &out->aps[out->count++];
            strlcpy(ap->ssid, ssid, sizeof(ap->ssid));
            ap->rssi = recs[i].rssi;
            ap->secure = recs[i].authmode != WIFI_AUTH_OPEN;
        }
    }
    esp_wifi_clear_ap_list();
    ESP_LOGI(TAG, "scan done: %d networks", out ? out->count : 0);
    if (out) {
        app_state_set_wifi_scan(out);   // APP_EVT_WIFI_SCAN_DONE
    }
    free(recs);
    free(out);
}

static void start_scan(void)
{
    if (s_scanning) {
        return;
    }
    s_scanning = true;
    esp_timer_stop(s_retry_timer);
    if (!(xEventGroupGetBits(s_events) & CONNECTED_BIT)) {
        esp_wifi_disconnect();   // 진행 중인 연결 시도를 멈춰야 스캔할 수 있다
    }
    const wifi_scan_config_t sc = { .show_hidden = false };
    esp_err_t err = esp_wifi_scan_start(&sc, false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "scan start: %s", esp_err_to_name(err));
        s_scanning = false;
        wifi_scan_t empty = { 0 };
        app_state_set_wifi_scan(&empty);
        schedule_retry(BACKOFF_MIN_MS);
    }
}

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    switch (id) {
    case WIFI_EVENT_STA_START:
        connect_now();
        break;

    case WIFI_EVENT_STA_DISCONNECTED: {
        const wifi_event_sta_disconnected_t *d = data;
        bool was_connected = xEventGroupGetBits(s_events) & CONNECTED_BIT;
        xEventGroupClearBits(s_events, CONNECTED_BIT);
        if (was_connected) {
            app_state_set_ip("");
            app_event_post(APP_EVT_WIFI_DISCONNECTED, NULL, 0);
        }
        if (!s_scanning && have_credentials()) {
            ESP_LOGW(TAG, "disconnected (reason %d), retry in %d ms", d->reason, s_backoff_ms);
            schedule_retry(s_backoff_ms);
            s_backoff_ms = s_backoff_ms * 2 > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : s_backoff_ms * 2;
        }
        break;
    }

    case WIFI_EVENT_SCAN_DONE:
        publish_scan_results();
        s_scanning = false;
        if (!(xEventGroupGetBits(s_events) & CONNECTED_BIT) && have_credentials()) {
            s_backoff_ms = BACKOFF_MIN_MS;
            schedule_retry(BACKOFF_MIN_MS);
        }
        break;

    default:
        break;
    }
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    const ip_event_got_ip_t *e = data;
    char ip[16];
    snprintf(ip, sizeof(ip), IPSTR, IP2STR(&e->ip_info.ip));
    ESP_LOGI(TAG, "connected, IP %s", ip);
    s_backoff_ms = BACKOFF_MIN_MS;
    app_state_set_ip(ip);
    xEventGroupSetBits(s_events, CONNECTED_BIT);
    app_event_post(APP_EVT_WIFI_CONNECTED, NULL, 0);
}

static void on_app_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == APP_EVT_REQ_WIFI_SCAN) {
        start_scan();
        return;
    }
    if (id == APP_EVT_SETTINGS_CHANGED) {
        app_settings_t s;
        if (settings_load(&s) != ESP_OK) {
            return;
        }
        if (strcmp(s.wifi_ssid, s_ssid) == 0 && strcmp(s.wifi_pass, s_pass) == 0) {
            return;
        }
        ESP_LOGI(TAG, "credentials changed, reconnecting");
        strlcpy(s_ssid, s.wifi_ssid, sizeof(s_ssid));
        strlcpy(s_pass, s.wifi_pass, sizeof(s_pass));
        esp_timer_stop(s_retry_timer);
        esp_wifi_disconnect();
        apply_config();
        s_backoff_ms = BACKOFF_MIN_MS;
        schedule_retry(500);
    }
}

esp_err_t wifi_mgr_start(const char *ssid, const char *pass)
{
    s_events = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_events, ESP_ERR_NO_MEM, TAG, "event group");
    strlcpy(s_ssid, ssid ? ssid : "", sizeof(s_ssid));
    strlcpy(s_pass, pass ? pass : "", sizeof(s_pass));

    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    s_netif = esp_netif_create_default_wifi_sta();
    ESP_RETURN_ON_FALSE(s_netif, ESP_FAIL, TAG, "default sta");
    esp_netif_set_hostname(s_netif, "smart-display");

    const wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "wifi init");

    const esp_timer_create_args_t timer_args = { .callback = on_retry_timer, .name = "wifi_retry" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_retry_timer), TAG, "timer");

    ESP_RETURN_ON_ERROR(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi_event, NULL), TAG, "wifi evt");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_got_ip, NULL), TAG, "ip evt");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(APP_EVENT, ESP_EVENT_ANY_ID, on_app_event, NULL), TAG, "app evt");

    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "storage");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "mode");
    ESP_RETURN_ON_ERROR(esp_wifi_set_country_code("KR", true), TAG, "country");
    apply_config();
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "start");

    if (!have_credentials()) {
        ESP_LOGW(TAG, "no SSID configured - set it in Settings");
    }
    return ESP_OK;
}

bool wifi_mgr_is_connected(void)
{
    return s_events && (xEventGroupGetBits(s_events) & CONNECTED_BIT);
}

bool wifi_mgr_wait_connected(uint32_t timeout_ms)
{
    if (!s_events) {
        return false;
    }
    EventBits_t bits = xEventGroupWaitBits(s_events, CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return bits & CONNECTED_BIT;
}
