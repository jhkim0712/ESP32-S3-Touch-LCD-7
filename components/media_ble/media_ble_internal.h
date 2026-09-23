// media_ble 컴포넌트 내부 공유 선언
#pragma once

#include "sdkconfig.h"
#if CONFIG_APP_BLE_MEDIA

#include <stdbool.h>
#include <stdint.h>
#include "host/ble_gap.h"
#include "media_ble.h"

// ---- HID (hid_media.c): 미디어 키 리모컨 (Android, 그 외 호스트) ----
int  hid_register_services(void);                       // GATT 서비스 등록 (호스트 시작 전)
void hid_on_subscribe(const struct ble_gap_event *event);
esp_err_t hid_send(uint16_t conn, media_cmd_t cmd);

// ---- AMS (ams_client.c): iOS Apple Media Service 클라이언트 ----
extern const ble_uuid128_t ams_svc_uuid;
void ams_start(uint16_t conn);                          // 암호화 후 서비스 탐색 시작
void ams_reset(void);                                   // 연결 끊김
bool ams_available(void);
esp_err_t ams_send(media_cmd_t cmd);
void ams_on_notify(const struct ble_gap_event *event);
void ams_tick(void);                                    // 1초마다: 재생 위치 추정

#endif // CONFIG_APP_BLE_MEDIA
