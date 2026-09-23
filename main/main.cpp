// Smart Display 진입점.
// 초기화 순서만 담당하고, 각 기능은 components/ 의 모듈이 소유한다.
// 단계별로 TODO 가 채워진다 (3단계: board/ui, 5단계: 서비스 태스크, 6단계: OTA).

#include "esp_app_desc.h"
#include "esp_log.h"
#include "nvs_flash.h"

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

    // [3단계] board_init()  : I2C, CH422G, RGB LCD, GT911, 백라이트
    // [3단계] ui_init()     : LVGL port, 테마, 위젯/슬라이드 뷰
    // [5단계] storage / net / services / media_ble / photo 태스크 시작
    // [6단계] ota_start()   : GitHub Release 주기 확인, 부팅 성공 시 rollback 취소
}
