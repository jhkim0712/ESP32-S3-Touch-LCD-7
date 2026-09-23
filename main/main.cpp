// Smart Display 진입점.
// 초기화 순서만 담당하고, 각 기능은 components/ 의 모듈이 소유한다.

#include <stdlib.h>
#include <time.h>

#include "app_state.h"
#include "app_storage.h"
#include "board.h"
#include "demo_data.h"
#include "ui.h"

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
    ESP_ERROR_CHECK(ui_init(&hw, settings.ui_mode, settings.rotation));

    ESP_ERROR_CHECK(esp_event_handler_register(APP_EVENT, APP_EVT_UI_MODE_CHANGED, on_ui_mode_changed, NULL));

    // SD 카드가 없어도 부팅은 계속한다 (전자앨범만 비활성)
    board_sdcard_mount();

    demo_data_start();

    ESP_LOGI(TAG, "free heap: internal %u KB, PSRAM %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

    // [5-2] Wi-Fi + NTP, [5-3] 날씨/주가, [5-4] 전자앨범, [5-5] BLE 음악 리모컨
    // [6단계] ota_start()   : GitHub Release 주기 확인, 부팅 성공 시 rollback 취소
}
