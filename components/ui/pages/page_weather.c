// 날씨: Open-Meteo 현재 날씨 (기온, 상태, 습도)
// 날씨 아이콘 이미지는 LittleFS 에셋 준비 후(5단계) 추가. 지금은 상태별 색상 배지로 표시.

#include "ui_internal.h"

#include <math.h>
#include <stdio.h>
#include <time.h>
#include "app_state.h"
#include "sdkconfig.h"

typedef struct {
    lv_obj_t *temp;
    lv_obj_t *cond;
    lv_obj_t *badge;
    lv_obj_t *humidity;
    lv_obj_t *updated;
} weather_view_t;

typedef struct {
    const char *text;
    uint32_t    color;
} wmo_desc_t;

// WMO weather interpretation codes (Open-Meteo)
static wmo_desc_t wmo_describe(int code)
{
    if (code == 0)                return (wmo_desc_t){ "Clear", 0xFFB000 };
    if (code <= 2)                return (wmo_desc_t){ "Partly cloudy", 0xF2C94C };
    if (code == 3)                return (wmo_desc_t){ "Overcast", 0x8B949E };
    if (code == 45 || code == 48) return (wmo_desc_t){ "Fog", 0x9DA5B4 };
    if (code >= 51 && code <= 57) return (wmo_desc_t){ "Drizzle", 0x6FB7FF };
    if (code >= 61 && code <= 67) return (wmo_desc_t){ "Rain", 0x3DA9FC };
    if (code >= 71 && code <= 77) return (wmo_desc_t){ "Snow", 0xE6EDF3 };
    if (code >= 80 && code <= 82) return (wmo_desc_t){ "Showers", 0x3DA9FC };
    if (code == 85 || code == 86) return (wmo_desc_t){ "Snow showers", 0xE6EDF3 };
    if (code >= 95)               return (wmo_desc_t){ "Thunderstorm", 0xB478FF };
    return (wmo_desc_t){ "Unknown", 0x8B949E };
}

static void weather_cb(lv_observer_t *o, lv_subject_t *s)
{
    lv_obj_t *root = lv_observer_get_target_obj(o);
    weather_view_t *v = lv_obj_get_user_data(root);
    weather_info_t w;
    app_state_get_weather(&w);

    if (!w.valid) {
        lv_label_set_text(v->temp, "--");
        lv_label_set_text(v->cond, "No data yet");
        lv_label_set_text(v->humidity, "");
        if (v->updated) {
            lv_label_set_text(v->updated, "");
        }
        return;
    }

    char num[16];
    ui_fmt_number(num, sizeof(num), w.temp_c, 1);
    lv_label_set_text_fmt(v->temp, "%s\xC2\xB0" "C", num);   // °C

    wmo_desc_t d = wmo_describe(w.wmo_code);
    lv_label_set_text(v->cond, d.text);
    lv_obj_set_style_bg_color(v->badge, lv_color_hex(d.color), 0);
    lv_label_set_text_fmt(v->humidity, LV_SYMBOL_TINT "  Humidity %d%%", w.humidity);

    if (v->updated) {
        struct tm tm;
        localtime_r(&w.updated_at, &tm);
        char buf[32];
        strftime(buf, sizeof(buf), "Updated %H:%M", &tm);
        lv_label_set_text(v->updated, buf);
    }
}

static void on_refresh(lv_event_t *e)
{
    app_event_post(APP_EVT_REQ_REFRESH, NULL, 0);
}

void ui_weather_create(lv_obj_t *parent, bool full)
{
    weather_view_t *v = lv_malloc_zeroed(sizeof(weather_view_t));
    lv_obj_set_user_data(parent, v);
    ui_free_user_data_on_delete(parent);

    lv_obj_t *head = lv_obj_create(parent);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    ui_card_title(head, LV_SYMBOL_HOME, "Weather  " CONFIG_APP_WEATHER_CITY);
    if (full) {
        lv_obj_t *btn = lv_button_create(head);
        lv_obj_set_style_bg_color(btn, UI_COLOR_CARD_ALT, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_add_event_cb(btn, on_refresh, LV_EVENT_CLICKED, NULL);
        lv_obj_center(ui_label(btn, UI_FONT_S, UI_COLOR_TEXT, LV_SYMBOL_REFRESH));
    }

    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 16, 0);
    lv_obj_set_style_pad_top(row, full ? 24 : 8, 0);

    v->badge = lv_obj_create(row);
    lv_obj_remove_style_all(v->badge);
    lv_obj_set_size(v->badge, full ? 72 : 44, full ? 72 : 44);
    lv_obj_set_style_radius(v->badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(v->badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(v->badge, UI_COLOR_DIM, 0);

    v->temp = ui_label(row, UI_FONT_XL, UI_COLOR_TEXT, "");
    v->cond = ui_label(parent, full ? UI_FONT_L : UI_FONT_M, UI_COLOR_TEXT, "");
    v->humidity = ui_label(parent, full ? UI_FONT_M : UI_FONT_S, UI_COLOR_DIM, "");
    if (full) {
        v->updated = ui_label(parent, UI_FONT_S, UI_COLOR_DIM, "");
    }

    lv_subject_add_observer_obj(&ui_subj_weather, weather_cb, parent, NULL);
}
