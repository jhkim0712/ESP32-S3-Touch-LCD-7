// Apple Media Service (AMS) 클라이언트 - iOS 에서 재생 중인 곡 정보와 원격 제어
// https://developer.apple.com/library/archive/documentation/CoreBluetooth/Reference/AppleMediaService_Reference/
//
// 순서: 서비스 탐색 → 특성 탐색 → Entity Update 의 CCCD 탐색 → 알림 구독
//       → 관심 속성 등록(Player: PlaybackInfo, Track: Artist/Album/Title/Duration)
// 알림 형식: [EntityID][AttributeID][Flags][UTF-8 값...]  (Flags bit0 = 값이 잘림)
// 모든 콜백은 NimBLE host 태스크에서 실행되고, ams_tick() 만 esp_timer 태스크에서 실행된다.

#include "sdkconfig.h"
#if CONFIG_APP_BLE_MEDIA   // 메모리 부족으로 기본 비활성 (main/Kconfig.projbuild)

#include "media_ble_internal.h"

#include <stdlib.h>
#include <string.h>
#include "app_state.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "host/ble_hs.h"

static const char *TAG = "ams";

// UUID 는 little-endian 바이트 순서
const ble_uuid128_t ams_svc_uuid = BLE_UUID128_INIT(       // 89D3502B-0F36-433A-8EF4-C502AD55F8DC
    0xDC, 0xF8, 0x55, 0xAD, 0x02, 0xC5, 0xF4, 0x8E, 0x3A, 0x43, 0x36, 0x0F, 0x2B, 0x50, 0xD3, 0x89);
static const ble_uuid128_t k_remote_cmd = BLE_UUID128_INIT( // 9B3C81D8-57B1-4A8A-B8DF-0E56F7CA51C2
    0xC2, 0x51, 0xCA, 0xF7, 0x56, 0x0E, 0xDF, 0xB8, 0x8A, 0x4A, 0xB1, 0x57, 0xD8, 0x81, 0x3C, 0x9B);
static const ble_uuid128_t k_entity_update = BLE_UUID128_INIT( // 2F7CABCE-808D-411F-9A0C-BB92BA96C102
    0x02, 0xC1, 0x96, 0xBA, 0x92, 0xBB, 0x0C, 0x9A, 0x1F, 0x41, 0x8D, 0x80, 0xCE, 0xAB, 0x7C, 0x2F);

enum { ENTITY_PLAYER = 0, ENTITY_QUEUE = 1, ENTITY_TRACK = 2 };
enum { PLAYER_PLAYBACK_INFO = 1 };
enum { TRACK_ARTIST = 0, TRACK_ALBUM = 1, TRACK_TITLE = 2, TRACK_DURATION = 3 };
enum { RC_PLAY = 0, RC_PAUSE = 1, RC_TOGGLE = 2, RC_NEXT = 3, RC_PREV = 4, RC_VOL_UP = 5, RC_VOL_DOWN = 6 };

static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_svc_start, s_svc_end;
static uint16_t s_rc_val, s_eu_val, s_eu_end, s_eu_cccd;
static uint16_t s_next_def_after_eu;
static bool     s_found_svc;
static volatile bool s_available;

// 곡 정보. 재생 위치는 마지막 PlaybackInfo 시점의 값 + 경과 시간으로 추정한다.
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static media_info_t s_media;
static float        s_elapsed_base;
static int64_t      s_elapsed_at_us;
static uint32_t     s_last_published_elapsed = UINT32_MAX;

static void publish(void)
{
    media_info_t copy;
    taskENTER_CRITICAL(&s_lock);
    copy = s_media;
    taskEXIT_CRITICAL(&s_lock);
    s_last_published_elapsed = copy.elapsed_s;
    app_state_set_media(&copy);
}

bool ams_available(void)
{
    return s_available;
}

void ams_reset(void)
{
    s_conn = BLE_HS_CONN_HANDLE_NONE;
    s_available = false;
    s_found_svc = false;
    s_rc_val = s_eu_val = s_eu_end = s_eu_cccd = 0;
    taskENTER_CRITICAL(&s_lock);
    memset(&s_media, 0, sizeof(s_media));
    taskEXIT_CRITICAL(&s_lock);
    publish();
}

// ---------------------------------------------------------------------------
// 등록 (쓰기는 한 번에 하나씩, 앞의 응답을 받은 뒤 다음 단계)
// ---------------------------------------------------------------------------

static int on_write(uint16_t conn, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    int step = (int)(intptr_t)arg;
    if (error->status != 0) {
        ESP_LOGW(TAG, "setup step %d failed: %d", step, error->status);
        return 0;
    }
    static const uint8_t player[] = { ENTITY_PLAYER, PLAYER_PLAYBACK_INFO };
    static const uint8_t track[] = { ENTITY_TRACK, TRACK_ARTIST, TRACK_ALBUM, TRACK_TITLE, TRACK_DURATION };
    switch (step) {
    case 0:   // CCCD 구독 완료 → Player 속성 등록
        ble_gattc_write_flat(conn, s_eu_val, player, sizeof(player), on_write, (void *)1);
        break;
    case 1:   // → Track 속성 등록
        ble_gattc_write_flat(conn, s_eu_val, track, sizeof(track), on_write, (void *)2);
        break;
    case 2:
        s_available = true;
        ESP_LOGI(TAG, "Apple Media Service ready");
        break;
    }
    return 0;
}

static void subscribe(uint16_t conn)
{
    static const uint8_t enable_notify[] = { 0x01, 0x00 };
    ble_gattc_write_flat(conn, s_eu_cccd, enable_notify, sizeof(enable_notify), on_write, (void *)0);
}

// ---------------------------------------------------------------------------
// 탐색
// ---------------------------------------------------------------------------

static int on_dsc(uint16_t conn, const struct ble_gatt_error *error, uint16_t chr_val_handle,
                  const struct ble_gatt_dsc *dsc, void *arg)
{
    if (error->status == 0 && ble_uuid_u16(&dsc->uuid.u) == BLE_GATT_DSC_CLT_CFG_UUID16) {
        s_eu_cccd = dsc->handle;
    } else if (error->status == BLE_HS_EDONE) {
        if (s_eu_cccd) {
            subscribe(conn);
        } else {
            ESP_LOGW(TAG, "entity update CCCD not found");
        }
    }
    return 0;
}

static int on_chr(uint16_t conn, const struct ble_gatt_error *error, const struct ble_gatt_chr *chr, void *arg)
{
    if (error->status == 0) {
        if (ble_uuid_cmp(&chr->uuid.u, &k_remote_cmd.u) == 0) {
            s_rc_val = chr->val_handle;
        } else if (ble_uuid_cmp(&chr->uuid.u, &k_entity_update.u) == 0) {
            s_eu_val = chr->val_handle;
        } else if (s_eu_val && !s_next_def_after_eu) {
            s_next_def_after_eu = chr->def_handle;   // Entity Update 다음 특성 → 디스크립터 범위 끝
        }
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (!s_eu_val || !s_rc_val) {
            ESP_LOGW(TAG, "AMS characteristics missing");
            return 0;
        }
        s_eu_end = s_next_def_after_eu ? s_next_def_after_eu - 1 : s_svc_end;
        ble_gattc_disc_all_dscs(conn, s_eu_val, s_eu_end, on_dsc, NULL);
    }
    return 0;
}

static int on_svc(uint16_t conn, const struct ble_gatt_error *error, const struct ble_gatt_svc *svc, void *arg)
{
    if (error->status == 0 && svc) {
        s_found_svc = true;
        s_svc_start = svc->start_handle;
        s_svc_end = svc->end_handle;
        return 0;
    }
    if (error->status == BLE_HS_EDONE) {
        if (!s_found_svc) {
            // Android 등: 곡 정보 없이 HID 미디어 키로만 제어
            ESP_LOGI(TAG, "no Apple Media Service - using HID media keys");
            taskENTER_CRITICAL(&s_lock);
            strlcpy(s_media.artist, "Media keys only (no track info)", sizeof(s_media.artist));
            taskEXIT_CRITICAL(&s_lock);
            publish();
            return 0;
        }
        s_next_def_after_eu = 0;
        ble_gattc_disc_all_chrs(conn, s_svc_start, s_svc_end, on_chr, NULL);
    }
    return 0;
}

void ams_start(uint16_t conn)
{
    s_conn = conn;
    s_found_svc = false;
    int rc = ble_gattc_disc_svc_by_uuid(conn, &ams_svc_uuid.u, on_svc, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "service discovery: %d", rc);
    }
}

// ---------------------------------------------------------------------------
// 알림 처리
// ---------------------------------------------------------------------------

static void copy_value(char *dst, size_t size, const uint8_t *src, size_t len)
{
    size_t n = len < size - 1 ? len : size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void ams_on_notify(const struct ble_gap_event *event)
{
    if (!s_eu_val || event->notify_rx.attr_handle != s_eu_val) {
        return;
    }
    uint8_t buf[260];
    uint16_t len = 0;
    if (ble_hs_mbuf_to_flat(event->notify_rx.om, buf, sizeof(buf), &len) != 0 || len < 3) {
        return;
    }
    uint8_t entity = buf[0], attr = buf[1];
    const uint8_t *value = buf + 3;
    size_t vlen = len - 3;
    char text[128];
    copy_value(text, sizeof(text), value, vlen);

    taskENTER_CRITICAL(&s_lock);
    if (entity == ENTITY_TRACK) {
        switch (attr) {
        case TRACK_ARTIST:   copy_value(s_media.artist, sizeof(s_media.artist), value, vlen); break;
        case TRACK_ALBUM:    copy_value(s_media.album, sizeof(s_media.album), value, vlen); break;
        case TRACK_TITLE:    copy_value(s_media.title, sizeof(s_media.title), value, vlen); break;
        case TRACK_DURATION: s_media.duration_s = (uint32_t)strtof(text, NULL); break;
        }
    } else if (entity == ENTITY_PLAYER && attr == PLAYER_PLAYBACK_INFO) {
        // "상태,속도,경과초"  상태: 0=일시정지 1=재생 2=되감기 3=빨리감기
        char *p = text;
        int state = (int)strtol(p, &p, 10);
        if (*p == ',') {
            strtof(p + 1, &p);   // 재생 속도 (사용 안 함)
        }
        float elapsed = (*p == ',') ? strtof(p + 1, NULL) : 0;
        s_media.state = state == 0 ? MEDIA_PAUSED : MEDIA_PLAYING;
        s_elapsed_base = elapsed;
        s_elapsed_at_us = esp_timer_get_time();
        s_media.elapsed_s = (uint32_t)elapsed;
    }
    taskEXIT_CRITICAL(&s_lock);
    publish();
}

void ams_tick(void)
{
    if (!s_available) {
        return;
    }
    bool changed = false;
    taskENTER_CRITICAL(&s_lock);
    if (s_media.state == MEDIA_PLAYING) {
        float now = s_elapsed_base + (float)(esp_timer_get_time() - s_elapsed_at_us) / 1e6f;
        uint32_t e = (uint32_t)now;
        if (s_media.duration_s && e > s_media.duration_s) {
            e = s_media.duration_s;
        }
        s_media.elapsed_s = e;
        changed = e != s_last_published_elapsed;
    }
    taskEXIT_CRITICAL(&s_lock);
    if (changed) {
        publish();
    }
}

esp_err_t ams_send(media_cmd_t cmd)
{
    static const uint8_t map[] = {
        [MEDIA_CMD_PLAY_PAUSE] = RC_TOGGLE,
        [MEDIA_CMD_NEXT]       = RC_NEXT,
        [MEDIA_CMD_PREV]       = RC_PREV,
        [MEDIA_CMD_VOL_UP]     = RC_VOL_UP,
        [MEDIA_CMD_VOL_DOWN]   = RC_VOL_DOWN,
    };
    if (!s_available || cmd >= sizeof(map)) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t b = map[cmd];
    int rc = ble_gattc_write_flat(s_conn, s_rc_val, &b, 1, NULL, NULL);
    if (rc != 0) {
        ESP_LOGW(TAG, "remote command %d: %d", b, rc);
        return ESP_FAIL;
    }
    return ESP_OK;
}

#endif // CONFIG_APP_BLE_MEDIA
