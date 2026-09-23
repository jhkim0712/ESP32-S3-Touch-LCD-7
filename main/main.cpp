// Smart Display 진입점.
// 초기화 순서만 담당하고, 각 기능은 components/ 의 모듈이 소유한다.

#include <stdlib.h>
#include <time.h>

#include "app_state.h"
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

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Smart Display v%s", esp_app_get_description()->version);

    init_nvs();
    setenv("TZ", CONFIG_APP_TZ, 1);   // KST-9: localtime() 이 한국 시간을 반환
    tzset();
    ESP_ERROR_CHECK(app_state_init());

    static board_handles_t hw;
    ESP_ERROR_CHECK(board_init(&hw));
    // [5단계] NVS 설정의 UI 모드/회전 적용
    ESP_ERROR_CHECK(ui_init(&hw, UI_MODE_WIDGET));

    // SD 카드가 없어도 부팅은 계속한다 (전자앨범만 비활성)
    board_sdcard_mount();

    demo_data_start();

    ESP_LOGI(TAG, "free heap: internal %u KB, PSRAM %u KB",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

    // [5단계] storage / net / services / media_ble / photo 태스크 시작
    // [6단계] ota_start()   : GitHub Release 주기 확인, 부팅 성공 시 rollback 취소
}
