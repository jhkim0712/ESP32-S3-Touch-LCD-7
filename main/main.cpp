// Smart Display 진입점.
// 초기화 순서만 담당하고, 각 기능은 components/ 의 모듈이 소유한다.

#include <stdlib.h>
#include <time.h>

#include "app_state.h"
#include "app_flickr.h"
#include "app_storage.h"
#include "board.h"
#include "media_ble.h"
#include "net.h"
#include "ota.h"
#include "photo.h"
#include "services.h"
#include "ui.h"
#include "web_server.h"

#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

static const char *TAG = "main";

static void init_nvs()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

// 상태 표시줄에서 위젯형/슬라이드형을 바꾸면 다음 부팅에도 유지되도록 저장
static void on_ui_mode_changed(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    app_settings_t s;
    if (settings_load(&s) == ESP_OK) {
        s.ui_mode = *static_cast<const ui_mode_t *>(data);
        settings_save(&s);
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Smart Display v%s", esp_app_get_description()->version);

    init_nvs();
    setenv("TZ", CONFIG_APP_TZ, 1);   // KST-9: localtime() 이 한국 시간을 반환
    tzset();
    ESP_ERROR_CHECK(app_state_init());

    if (storage_init() != ESP_OK) {
        ESP_LOGW(TAG, "LittleFS unavailable (fonts/icons disabled)");
    }
    app_settings_t settings;
    ESP_ERROR_CHECK(settings_load(&settings));

    static board_handles_t hw;
    board_config_t board_cfg = BOARD_CONFIG_DEFAULT();
    board_cfg.lcd_num_fbs = ui_board_num_fbs(settings.rotation);
    ESP_ERROR_CHECK(board_init(&board_cfg, &hw));
    ESP_ERROR_CHECK(ui_init(&hw, &settings));

    ESP_ERROR_CHECK(esp_event_handler_register(APP_EVENT, APP_EVT_UI_MODE_CHANGED, on_ui_mode_changed, NULL));

    // Wi-Fi 는 SSID 가 비어 있어도 시작한다 (설정 페이지 스캔에 필요)
    ESP_ERROR_CHECK(wifi_mgr_start(settings.wifi_ssid, settings.wifi_pass));
    ESP_ERROR_CHECK(time_sync_start());
    ESP_ERROR_CHECK(ota_init());           // 업데이트 직후면 60초 뒤 롤백 취소
    ESP_ERROR_CHECK(app_flickr_init());
    ESP_ERROR_CHECK(net_worker_start());   // 날씨/주가/Flickr 사진 피드/업데이트 확인
    if (web_server_start() != ESP_OK) {    // 브라우저 설정 페이지 (없어도 기기는 동작)
        ESP_LOGW(TAG, "web server unavailable");
    }

    // SD 카드가 없어도 부팅은 계속한다 (전자앨범은 /storage/photos 를 대신 찾는다)
    board_sdcard_mount();
    uint16_t photo_w, photo_h;
    ui_photo_area(settings.rotation, &photo_w, &photo_h);
    ESP_ERROR_CHECK(photo_start(CONFIG_APP_PHOTO_DIR, settings.photo_interval_s, photo_w, photo_h));

    ESP_LOGI(TAG, "free heap: internal %u KB, PSRAM %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

    ESP_ERROR_CHECK(media_ble_start("Smart Display"));   // BLE 음악 리모컨
}
