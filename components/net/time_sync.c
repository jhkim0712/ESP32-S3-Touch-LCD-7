// SNTP 시간 동기화. 표시용 시간대(TZ=KST-9)는 main 에서 설정한다.
// 동기화되면 APP_EVT_TIME_SYNCED 를 보내고, 이후에는 lwIP 가 1시간마다 다시 맞춘다
// (CONFIG_LWIP_SNTP_UPDATE_DELAY). IP 를 새로 받았는데 아직 동기화 전이면 바로 재시도한다.

#include "net.h"

#include <time.h>

#include "app_state.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "sdkconfig.h"

static const char *TAG = "sntp";

static volatile bool s_synced;

static void on_sync(struct timeval *tv)
{
    bool first = !s_synced;
    s_synced = true;
    if (first) {
        time_t now = tv->tv_sec;
        struct tm tm;
        localtime_r(&now, &tm);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        ESP_LOGI(TAG, "time synced: %s (%s)", buf, CONFIG_APP_TZ);
    }
    app_event_post(APP_EVT_TIME_SYNCED, NULL, 0);
}

static void on_got_ip(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (!s_synced) {
        esp_sntp_restart();
    }
}

esp_err_t time_sync_start(void)
{
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        2, ESP_SNTP_SERVER_LIST(CONFIG_APP_NTP_SERVER_1, CONFIG_APP_NTP_SERVER_2));
    cfg.sync_cb = on_sync;
    ESP_RETURN_ON_ERROR(esp_netif_sntp_init(&cfg), TAG, "sntp init");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_got_ip, NULL), TAG, "ip evt");
    return ESP_OK;
}

bool time_is_synced(void)
{
    return s_synced;
}
