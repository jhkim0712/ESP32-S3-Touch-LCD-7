// BLE 음악 컨트롤러 (NimBLE, peripheral).
//
// ESP32-S3 는 Bluetooth Classic 이 없으므로 A2DP/AVRCP 는 사용할 수 없다. 대신:
//  - iOS     : Apple Media Service(AMS) 클라이언트 → 곡 정보 수신 + 원격 제어
//  - Android : BLE HID Consumer Control(미디어 키) → 재생/일시정지/이전/다음 제어
//              (곡 정보는 표준 BLE 서비스가 없어 companion 앱 또는 커스텀 GATT 필요)
#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MEDIA_CMD_PLAY_PAUSE,
    MEDIA_CMD_NEXT,
    MEDIA_CMD_PREV,
    MEDIA_CMD_VOL_UP,
    MEDIA_CMD_VOL_DOWN,
} media_cmd_t;

typedef enum {
    BLE_STATE_IDLE,
    BLE_STATE_ADVERTISING,
    BLE_STATE_CONNECTED,
    BLE_STATE_BONDED,
} ble_state_t;

esp_err_t   media_ble_start(const char *device_name);
esp_err_t   media_ble_send(media_cmd_t cmd);   // AMS 가능 시 AMS, 아니면 HID 키
ble_state_t media_ble_state(void);
esp_err_t   media_ble_forget_bond(void);

#ifdef __cplusplus
}
#endif
