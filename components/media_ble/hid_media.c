// BLE HID (HOGP) Consumer Control: 재생/일시정지, 다음, 이전, 볼륨 +/-
// Android(및 Windows/macOS) 는 이 미디어 키로 현재 재생 중인 앱을 제어한다. 곡 정보는 받을 수 없다.
// 서비스: HID(0x1812), Battery(0x180F), Device Information(0x180A, PnP ID)

#include "sdkconfig.h"
#if CONFIG_APP_BLE_MEDIA   // 메모리 부족으로 기본 비활성 (main/Kconfig.projbuild)

#include "media_ble_internal.h"

#include "esp_check.h"
#include "esp_log.h"
#include "host/ble_hs.h"

#define REPORT_ID_CONSUMER  1

static const char *TAG = "ble_hid";

// Report ID 1: 5개 비트(재생/일시정지, 다음, 이전, 볼륨+, 볼륨-) + 3비트 패딩 = 1바이트
static const uint8_t k_report_map[] = {
    0x05, 0x0C,         // Usage Page (Consumer)
    0x09, 0x01,         // Usage (Consumer Control)
    0xA1, 0x01,         // Collection (Application)
    0x85, REPORT_ID_CONSUMER,
    0x15, 0x00,         //   Logical Minimum (0)
    0x25, 0x01,         //   Logical Maximum (1)
    0x75, 0x01,         //   Report Size (1)
    0x95, 0x05,         //   Report Count (5)
    0x09, 0xCD,         //   Usage (Play/Pause)
    0x09, 0xB5,         //   Usage (Scan Next Track)
    0x09, 0xB6,         //   Usage (Scan Previous Track)
    0x09, 0xE9,         //   Usage (Volume Increment)
    0x09, 0xEA,         //   Usage (Volume Decrement)
    0x81, 0x02,         //   Input (Data, Var, Abs)
    0x75, 0x03,         //   Report Size (3)
    0x95, 0x01,         //   Report Count (1)
    0x81, 0x03,         //   Input (Const) - padding
    0xC0,               // End Collection
};

static const uint8_t k_hid_info[] = { 0x11, 0x01, 0x00, 0x02 };        // bcdHID 1.11, country 0, normally connectable
static const uint8_t k_report_ref[] = { REPORT_ID_CONSUMER, 0x01 };    // input report
static const uint8_t k_pnp_id[] = { 0x02, 0x3A, 0x30, 0x01, 0x00, 0x00, 0x01 };   // USB VID 0x303A (Espressif)
static const char    k_manufacturer[] = "DIY";
static uint8_t       s_protocol_mode = 1;     // report protocol
static uint8_t       s_battery = 100;         // 전원 연결 기기이므로 항상 100%
static uint8_t       s_report;
static uint16_t      s_report_handle;

static int append(struct ble_gatt_access_ctxt *ctxt, const void *data, uint16_t len)
{
    return os_mbuf_append(ctxt->om, data, len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int hid_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
        if (ble_uuid_u16(ctxt->dsc->uuid) == 0x2908) {
            return append(ctxt, k_report_ref, sizeof(k_report_ref));
        }
        return BLE_ATT_ERR_UNLIKELY;
    }

    uint16_t uuid = ble_uuid_u16(ctxt->chr->uuid);
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (uuid == 0x2A4E && OS_MBUF_PKTLEN(ctxt->om) == 1) {
            os_mbuf_copydata(ctxt->om, 0, 1, &s_protocol_mode);
        }
        return 0;   // Control Point (suspend/exit suspend) 등은 무시
    }

    switch (uuid) {
    case 0x2A4A: return append(ctxt, k_hid_info, sizeof(k_hid_info));
    case 0x2A4B: return append(ctxt, k_report_map, sizeof(k_report_map));
    case 0x2A4E: return append(ctxt, &s_protocol_mode, 1);
    case 0x2A4D: return append(ctxt, &s_report, 1);
    case 0x2A19: return append(ctxt, &s_battery, 1);
    case 0x2A29: return append(ctxt, k_manufacturer, sizeof(k_manufacturer) - 1);
    case 0x2A50: return append(ctxt, k_pnp_id, sizeof(k_pnp_id));
    default:     return BLE_ATT_ERR_UNLIKELY;
    }
}

static const struct ble_gatt_svc_def k_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),   // HID
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2A4A), .access_cb = hid_access, .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(0x2A4B), .access_cb = hid_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC },
            { .uuid = BLE_UUID16_DECLARE(0x2A4C), .access_cb = hid_access, .flags = BLE_GATT_CHR_F_WRITE_NO_RSP },
            { .uuid = BLE_UUID16_DECLARE(0x2A4E), .access_cb = hid_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D),   // Input report
                .access_cb = hid_access,
                .val_handle = &s_report_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    { .uuid = BLE_UUID16_DECLARE(0x2908), .att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC,
                      .access_cb = hid_access },
                    { 0 },
                },
            },
            { 0 },
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),   // Battery
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2A19), .access_cb = hid_access,
              .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY },
            { 0 },
        },
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A),   // Device Information
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = BLE_UUID16_DECLARE(0x2A29), .access_cb = hid_access, .flags = BLE_GATT_CHR_F_READ },
            { .uuid = BLE_UUID16_DECLARE(0x2A50), .access_cb = hid_access, .flags = BLE_GATT_CHR_F_READ },
            { 0 },
        },
    },
    { 0 },
};

int hid_register_services(void)
{
    int rc = ble_gatts_count_cfg(k_services);
    if (rc == 0) {
        rc = ble_gatts_add_svcs(k_services);
    }
    return rc;
}

void hid_on_subscribe(const struct ble_gap_event *event)
{
    if (event->subscribe.attr_handle == s_report_handle) {
        ESP_LOGI(TAG, "media key reports %s", event->subscribe.cur_notify ? "enabled" : "disabled");
    }
}

static esp_err_t notify(uint16_t conn, uint8_t value)
{
    s_report = value;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&s_report, 1);
    ESP_RETURN_ON_FALSE(om, ESP_ERR_NO_MEM, TAG, "mbuf");
    int rc = ble_gatts_notify_custom(conn, s_report_handle, om);
    ESP_RETURN_ON_FALSE(rc == 0, ESP_FAIL, TAG, "notify: %d", rc);
    return ESP_OK;
}

esp_err_t hid_send(uint16_t conn, media_cmd_t cmd)
{
    static const uint8_t bits[] = {
        [MEDIA_CMD_PLAY_PAUSE] = 1 << 0,
        [MEDIA_CMD_NEXT]       = 1 << 1,
        [MEDIA_CMD_PREV]       = 1 << 2,
        [MEDIA_CMD_VOL_UP]     = 1 << 3,
        [MEDIA_CMD_VOL_DOWN]   = 1 << 4,
    };
    ESP_RETURN_ON_FALSE(cmd < sizeof(bits), ESP_ERR_INVALID_ARG, TAG, "cmd");
    ESP_RETURN_ON_ERROR(notify(conn, bits[cmd]), TAG, "press");
    return notify(conn, 0);   // 키 뗌
}

#endif // CONFIG_APP_BLE_MEDIA
