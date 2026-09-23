// BLE 음악 리모컨 (NimBLE peripheral)
//
// 광고: HID 서비스(0x1812) + AMS 서비스 요청(solicitation) → 휴대폰 블루투스 설정에서 페어링
// 연결 → 보안 요청(bonding, Just Works) → 암호화되면
//   - iOS    : AMS 서비스를 찾아 곡 정보 수신 + 원격 제어 (ams_client.c)
//   - Android: AMS 가 없으므로 HID 미디어 키로 제어 (hid_media.c)
// 명령 요청(APP_EVT_REQ_MEDIA_CMD)은 AMS 가 있으면 AMS, 없으면 HID 로 보낸다.

//
// 메모리 부족으로 기본 비활성: CONFIG_APP_BLE_MEDIA (main/Kconfig.projbuild) 를 켜야 컴파일된다.

#include "media_ble.h"
#include "sdkconfig.h"

#if CONFIG_APP_BLE_MEDIA

#include "media_ble_internal.h"

#include <string.h>
#include "app_state.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#define APPEARANCE_HID_GENERIC  0x03C0

static const char *TAG = "ble";

void ble_store_config_init(void);   // NimBLE store (NVS) - 공개 헤더 없음

static char               s_name[32];
static uint8_t            s_own_addr_type;
static volatile uint16_t  s_conn = BLE_HS_CONN_HANDLE_NONE;
static volatile ble_state_t s_state = BLE_STATE_IDLE;
static esp_timer_handle_t s_tick_timer;

static void set_state(ble_state_t st)
{
    s_state = st;
    app_event_post(APP_EVT_BLE_STATE, &st, sizeof(st));
}

static int gap_event(struct ble_gap_event *event, void *arg);

static void advertise(void)
{
    static const ble_uuid16_t hid_uuid = BLE_UUID16_INIT(0x1812);

    struct ble_hs_adv_fields f = { 0 };
    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    f.appearance = APPEARANCE_HID_GENERIC;
    f.appearance_is_present = 1;
    f.uuids16 = &hid_uuid;
    f.num_uuids16 = 1;
    f.uuids16_is_complete = 1;
    f.sol_uuids128 = &ams_svc_uuid;      // iOS 에 AMS 사용을 요청
    f.sol_num_uuids128 = 1;
    int rc = ble_gap_adv_set_fields(&f);
    if (rc != 0) {
        ESP_LOGE(TAG, "adv fields: %d", rc);
        return;
    }

    struct ble_hs_adv_fields rsp = { 0 };
    rsp.name = (const uint8_t *)s_name;
    rsp.name_len = strlen(s_name);
    rsp.name_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&rsp);

    struct ble_gap_adv_params p = { 0 };
    p.conn_mode = BLE_GAP_CONN_MODE_UND;
    p.disc_mode = BLE_GAP_DISC_MODE_GEN;
    p.itvl_min = BLE_GAP_ADV_ITVL_MS(100);
    p.itvl_max = BLE_GAP_ADV_ITVL_MS(200);
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &p, gap_event, NULL);
    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "adv start: %d", rc);
        return;
    }
    ESP_LOGI(TAG, "advertising as \"%s\"", s_name);
    set_state(BLE_STATE_ADVERTISING);
}

static int on_mtu(uint16_t conn, const struct ble_gatt_error *error, uint16_t mtu, void *arg)
{
    ESP_LOGI(TAG, "MTU %u", mtu);
    ams_start(conn);   // 곡 정보가 길어서 MTU 를 키운 뒤 AMS 탐색
    return 0;
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn = event->connect.conn_handle;
            ESP_LOGI(TAG, "connected");
            set_state(BLE_STATE_CONNECTED);
            ble_gap_security_initiate(s_conn);   // 페어링/암호화 요청 (HID, AMS 모두 필요)
        } else {
            advertise();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "disconnected (reason 0x%x)", event->disconnect.reason);
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        ams_reset();
        advertise();
        break;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status == 0) {
            ESP_LOGI(TAG, "encrypted (bonded)");
            set_state(BLE_STATE_BONDED);
            ble_gattc_exchange_mtu(event->enc_change.conn_handle, on_mtu, NULL);
        } else {
            ESP_LOGW(TAG, "encryption failed: %d", event->enc_change.status);
        }
        break;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // 휴대폰에서 페어링을 지웠다가 다시 연결한 경우: 기존 키를 지우고 다시 페어링
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    case BLE_GAP_EVENT_SUBSCRIBE:
        hid_on_subscribe(event);
        break;

    case BLE_GAP_EVENT_NOTIFY_RX:
        ams_on_notify(event);
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        advertise();
        break;

    default:
        break;
    }
    return 0;
}

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    advertise();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "host reset, reason %d", reason);
}

static void host_task(void *arg)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void on_app_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (id == APP_EVT_REQ_MEDIA_CMD) {
        media_ble_send(*(const media_cmd_t *)data);
    } else if (id == APP_EVT_REQ_BLE_UNPAIR) {
        media_ble_forget_bond();
    }
}

static void on_tick(void *arg)
{
    ams_tick();
}

esp_err_t media_ble_start(const char *device_name)
{
    strlcpy(s_name, device_name, sizeof(s_name));

    ESP_RETURN_ON_ERROR(nimble_port_init(), TAG, "nimble init");

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;   // Just Works (화면에 PIN 입력 없음)
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_RETURN_ON_FALSE(hid_register_services() == 0, ESP_FAIL, TAG, "gatt services");
    ble_svc_gap_device_name_set(s_name);
    ble_svc_gap_device_appearance_set(APPEARANCE_HID_GENERIC);
    ble_store_config_init();

    ESP_RETURN_ON_ERROR(esp_event_handler_register(APP_EVENT, ESP_EVENT_ANY_ID, on_app_event, NULL), TAG, "evt");
    const esp_timer_create_args_t targs = { .callback = on_tick, .name = "ams_tick" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&targs, &s_tick_timer), TAG, "timer");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_tick_timer, 1000 * 1000), TAG, "timer start");

    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

esp_err_t media_ble_send(media_cmd_t cmd)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE) {
        ESP_LOGW(TAG, "not connected");
        return ESP_ERR_INVALID_STATE;
    }
    return ams_available() ? ams_send(cmd) : hid_send(s_conn, cmd);
}

ble_state_t media_ble_state(void)
{
    return s_state;
}

esp_err_t media_ble_forget_bond(void)
{
    ESP_LOGI(TAG, "forgetting all bonded devices");
    if (s_conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    int rc = ble_store_clear();
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

#else   // !CONFIG_APP_BLE_MEDIA: 기능 꺼짐 - 다른 모듈이 그대로 호출할 수 있도록 빈 구현

#include "esp_log.h"

esp_err_t media_ble_start(const char *device_name)
{
    ESP_LOGI("ble", "BLE music remote disabled (CONFIG_APP_BLE_MEDIA=n)");
    return ESP_OK;
}

esp_err_t media_ble_send(media_cmd_t cmd)
{
    return ESP_ERR_NOT_SUPPORTED;
}

ble_state_t media_ble_state(void)
{
    return BLE_STATE_IDLE;
}

esp_err_t media_ble_forget_bond(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

#endif // CONFIG_APP_BLE_MEDIA
