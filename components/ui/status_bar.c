// 상태 표시줄: lv_layer_top 에 두어 화면(모드)이 바뀌어도 유지된다.
//   [Wi-Fi] [BT]        요일 날짜 시각        [모드 전환] [설정]

#include "ui.h"
#include "ui_internal.h"

#include <time.h>
#include "media_ble.h"
#include "sdkconfig.h"

static lv_obj_t *s_mode_label;

static void wifi_cb(lv_observer_t *o, lv_subject_t *s)
{
    lv_obj_t *icon = lv_observer_get_target_obj(o);
    lv_obj_set_style_text_color(icon, lv_subject_get_int(s) ? UI_COLOR_ACCENT : UI_COLOR_DIM, 0);
}

static void __attribute__((unused)) ble_cb(lv_observer_t *o, lv_subject_t *s)
{
    lv_obj_t *icon = lv_observer_get_target_obj(o);
    int32_t st = lv_subject_get_int(s);
    lv_color_t c = st >= BLE_STATE_CONNECTED ? UI_COLOR_ACCENT
                 : st == BLE_STATE_ADVERTISING ? UI_COLOR_WARM : UI_COLOR_DIM;
    lv_obj_set_style_text_color(icon, c, 0);
}

static void time_cb(lv_observer_t *o, lv_subject_t *s)
{
    lv_obj_t *label = lv_observer_get_target_obj(o);
    time_t now = lv_subject_get_int(s);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year + 1900 < 2025) {
        lv_label_set_text(label, TR("Waiting for time sync", "시간 동기화 대기 중"));
        return;
    }
    char date[40];
    ui_fmt_date(date, sizeof(date), &tm, false);
    lv_label_set_text_fmt(label, "%s  %02d:%02d", date, tm.tm_hour, tm.tm_min);
}

static void on_mode_click(lv_event_t *e)
{
    ui_set_mode(ui_get_mode() == UI_MODE_WIDGET ? UI_MODE_SLIDE : UI_MODE_WIDGET);
}

static void on_settings_click(lv_event_t *e)
{
    ui_settings_open();
}

static lv_obj_t *bar_button(lv_obj_t *bar, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_button_create(bar);
    lv_obj_set_height(btn, UI_STATUS_BAR_H - 10);
    lv_obj_set_style_bg_color(btn, UI_COLOR_CARD_ALT, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

void ui_status_bar_create(void)
{
    lv_obj_t *bar = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), UI_STATUS_BAR_H);
    lv_obj_set_style_bg_color(bar, UI_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(bar, 16, 0);
    lv_obj_set_style_pad_column(bar, 16, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *wifi = ui_label(bar, UI_FONT_M, UI_COLOR_DIM, LV_SYMBOL_WIFI);
    lv_subject_add_observer_obj(&ui_subj_wifi, wifi_cb, wifi, NULL);

#if CONFIG_APP_BLE_MEDIA
    lv_obj_t *ble = ui_label(bar, UI_FONT_M, UI_COLOR_DIM, LV_SYMBOL_BLUETOOTH);
    lv_subject_add_observer_obj(&ui_subj_ble, ble_cb, ble, NULL);
#endif

    lv_obj_t *clock = ui_label(bar, UI_FONT_M, UI_COLOR_TEXT, "");
    lv_obj_set_flex_grow(clock, 1);
    lv_obj_set_style_text_align(clock, LV_TEXT_ALIGN_CENTER, 0);
    lv_subject_add_observer_obj(&ui_subj_time, time_cb, clock, NULL);

    lv_obj_t *btn = bar_button(bar, on_mode_click);
    s_mode_label = ui_label(btn, UI_FONT_S, UI_COLOR_TEXT, "");
    lv_obj_center(s_mode_label);

    lv_obj_t *gear = bar_button(bar, on_settings_click);
    lv_obj_center(ui_label(gear, UI_FONT_M, UI_COLOR_TEXT, LV_SYMBOL_SETTINGS));
}

void ui_status_bar_set_mode(ui_mode_t mode)
{
    // 버튼에는 "전환될" 모드를 표시한다
    lv_label_set_text(s_mode_label, mode == UI_MODE_WIDGET ? TR(LV_SYMBOL_RIGHT "  Slides", LV_SYMBOL_RIGHT "  슬라이드")
                                                           : TR(LV_SYMBOL_LIST "  Widgets", LV_SYMBOL_LIST "  위젯"));
}
