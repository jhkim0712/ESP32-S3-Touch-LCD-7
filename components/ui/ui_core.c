// LVGL 포팅과 UI 루트.
//
// 렌더링 방식 (800x480 RGB565, PSRAM 프레임버퍼 2개)
//   - direct_mode + avoid_tearing: LVGL 이 RGB 드라이버의 프레임버퍼에 직접 그리고
//     VSYNC 에 맞춰 두 버퍼를 교대로 표시한다. 별도 draw buffer 가 없어 PSRAM 복사가 줄어든다.
//   - bb_mode: RGB 드라이버의 bounce buffer 사용 (board_lcd.c)
//
// 데이터 흐름
//   서비스 → app_state_set_*() → APP_EVENT → on_app_event() → lv_subject 갱신
//   → 각 위젯의 observer 가 자기 자신을 다시 그림. 위젯이 삭제되면 observer 도 자동 해제되므로
//   위젯형/슬라이드형 화면을 새로 만들어도 별도 등록/해제 코드가 필요 없다.

#include "ui.h"
#include "ui_internal.h"

#include <inttypes.h>
#include <string.h>
#include <time.h>
#include "app_state.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "media_ble.h"
#include "photo.h"

static const char *TAG = "ui";

lv_subject_t ui_subj_time;
lv_subject_t ui_subj_weather;
lv_subject_t ui_subj_stocks;
lv_subject_t ui_subj_media;
lv_subject_t ui_subj_photo;
lv_subject_t ui_subj_wifi;
lv_subject_t ui_subj_ble;

static lv_display_t *s_disp;
static ui_mode_t     s_mode;

static void subjects_init(void)
{
    lv_subject_init_int(&ui_subj_time, (int32_t)time(NULL));
    lv_subject_init_int(&ui_subj_weather, 0);
    lv_subject_init_int(&ui_subj_stocks, 0);
    lv_subject_init_int(&ui_subj_media, 0);
    lv_subject_init_pointer(&ui_subj_photo, NULL);
    lv_subject_init_int(&ui_subj_wifi, 0);
    lv_subject_init_int(&ui_subj_ble, BLE_STATE_IDLE);
}

static void bump(lv_subject_t *s)
{
    lv_subject_set_int(s, lv_subject_get_int(s) + 1);
}

static void clock_tick(lv_timer_t *t)
{
    lv_subject_set_int(&ui_subj_time, (int32_t)time(NULL));
}

typedef struct {
    int32_t id;
    uint8_t data[];     // 이벤트 데이터 복사본
} ui_msg_t;

// LVGL 태스크에서 실행: subject 갱신 → observer 들이 화면을 다시 그림
static void apply_event(void *p)
{
    ui_msg_t *msg = p;
    const void *data = msg->data;
    switch (msg->id) {
    case APP_EVT_WIFI_CONNECTED:    lv_subject_set_int(&ui_subj_wifi, 1); break;
    case APP_EVT_WIFI_DISCONNECTED: lv_subject_set_int(&ui_subj_wifi, 0); break;
    case APP_EVT_TIME_SYNCED:       lv_subject_set_int(&ui_subj_time, (int32_t)time(NULL)); break;
    case APP_EVT_WEATHER_UPDATED:   bump(&ui_subj_weather); break;
    case APP_EVT_STOCKS_UPDATED:    bump(&ui_subj_stocks); break;
    case APP_EVT_MEDIA_UPDATED:     bump(&ui_subj_media); break;
    case APP_EVT_BLE_STATE:         lv_subject_set_int(&ui_subj_ble, *(const ble_state_t *)data); break;
    case APP_EVT_PHOTO_READY:
        lv_subject_set_pointer(&ui_subj_photo, (void *)*(const photo_frame_t *const *)data);
        break;
    case APP_EVT_OTA_AVAILABLE:     ui_ota_popup_show((const ota_release_t *)data); break;
    case APP_EVT_OTA_PROGRESS:      ui_ota_popup_progress(*(const int *)data); break;
    case APP_EVT_OTA_DONE:          ui_ota_popup_done(*(const esp_err_t *)data); break;
    default: break;
    }
    lv_free(msg);
}

// 기본 이벤트 루프 태스크에서 호출된다. 이 태스크는 스택이 작아(약 3.5KB) observer 가
// 위젯을 만드는 작업을 여기서 하면 넘칠 수 있으므로, LVGL 태스크(8KB)로 넘겨서 처리한다.
static void on_app_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    size_t size = 0;
    switch (id) {
    case APP_EVT_WIFI_CONNECTED:
    case APP_EVT_WIFI_DISCONNECTED:
    case APP_EVT_TIME_SYNCED:
    case APP_EVT_WEATHER_UPDATED:
    case APP_EVT_STOCKS_UPDATED:
    case APP_EVT_MEDIA_UPDATED:     break;
    case APP_EVT_BLE_STATE:         size = sizeof(ble_state_t); break;
    case APP_EVT_PHOTO_READY:       size = sizeof(const photo_frame_t *); break;
    case APP_EVT_OTA_AVAILABLE:     size = sizeof(ota_release_t); break;
    case APP_EVT_OTA_PROGRESS:      size = sizeof(int); break;
    case APP_EVT_OTA_DONE:          size = sizeof(esp_err_t); break;
    default:
        return;   // UI → 서비스 요청 이벤트는 무시
    }

    if (!lvgl_port_lock(1000)) {
        ESP_LOGW(TAG, "LVGL lock timeout (event %" PRIi32 ")", id);
        return;
    }
    ui_msg_t *msg = lv_malloc(sizeof(ui_msg_t) + size);
    if (msg) {
        msg->id = id;
        if (size) {
            memcpy(msg->data, data, size);
        }
        lv_async_call(apply_event, msg);
    }
    lvgl_port_unlock();
}

static lv_obj_t *build_screen(ui_mode_t mode)
{
    return mode == UI_MODE_SLIDE ? ui_slide_view_create() : ui_widget_view_create();
}

esp_err_t ui_init(const board_handles_t *hw, ui_mode_t initial_mode)
{
    ESP_RETURN_ON_FALSE(hw && hw->lcd_panel && hw->touch, ESP_ERR_INVALID_ARG, TAG, "hw");

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 4;
    port_cfg.task_stack = 8 * 1024;
    port_cfg.task_affinity = 1;          // core 1: 렌더링 전용 (Wi-Fi/BLE 는 core 0)
    port_cfg.task_max_sleep_ms = 500;
    port_cfg.timer_period_ms = 5;
    ESP_RETURN_ON_ERROR(lvgl_port_init(&port_cfg), TAG, "lvgl port init");

    const lvgl_port_display_cfg_t disp_cfg = {
        .panel_handle = hw->lcd_panel,
        .buffer_size = BOARD_LCD_H_RES * BOARD_LCD_V_RES,
        .double_buffer = true,
        .hres = BOARD_LCD_H_RES,
        .vres = BOARD_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_spiram = true,
            .swap_bytes = false,
            .direct_mode = true,
        },
    };
    const lvgl_port_display_rgb_cfg_t rgb_cfg = {
        .flags = {
            .bb_mode = true,
            .avoid_tearing = true,
        },
    };
    s_disp = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    ESP_RETURN_ON_FALSE(s_disp, ESP_FAIL, TAG, "add rgb display");

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = s_disp,
        .handle = hw->touch,
    };
    ESP_RETURN_ON_FALSE(lvgl_port_add_touch(&touch_cfg), ESP_FAIL, TAG, "add touch");

    ESP_RETURN_ON_FALSE(lvgl_port_lock(0), ESP_FAIL, TAG, "lock");
    ui_theme_init(s_disp);
    subjects_init();
    ui_status_bar_create();

    s_mode = initial_mode;
    lv_obj_t *old = lv_screen_active();
    lv_screen_load(build_screen(s_mode));
    lv_obj_delete(old);
    ui_status_bar_set_mode(s_mode);

    lv_timer_create(clock_tick, 1000, NULL);
    lvgl_port_unlock();

    ESP_RETURN_ON_ERROR(esp_event_handler_register(APP_EVENT, ESP_EVENT_ANY_ID, on_app_event, NULL),
                        TAG, "event handler");

    // 첫 프레임이 그려진 뒤 백라이트를 켜서 부팅 직후 노이즈 화면을 감춘다.
    vTaskDelay(pdMS_TO_TICKS(100));
    board_backlight_set(true);

    ESP_LOGI(TAG, "LVGL %d.%d.%d ready, %s mode", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR,
             LVGL_VERSION_PATCH, s_mode == UI_MODE_SLIDE ? "slide" : "widget");
    return ESP_OK;
}

void ui_set_mode(ui_mode_t mode)
{
    if (!lvgl_port_lock(0)) {   // 재귀 mutex: LVGL 이벤트 콜백 안에서 호출해도 안전
        return;
    }
    if (mode != s_mode) {
        s_mode = mode;
        lv_screen_load_anim(build_screen(mode), LV_SCREEN_LOAD_ANIM_FADE_IN, 250, 0, true);
        ui_status_bar_set_mode(mode);
        app_event_post(APP_EVT_UI_MODE_CHANGED, &mode, sizeof(mode));
    }
    lvgl_port_unlock();
}

ui_mode_t ui_get_mode(void)
{
    return s_mode;
}
