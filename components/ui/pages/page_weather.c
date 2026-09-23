// 날씨: Open-Meteo 현재 날씨 (기온, 상태, 습도)
// 아이콘은 Weather Icons TTF(/storage/fonts/weather-icons.ttf) 글리프로 그린다 (낮/밤 구분).
// 폰트 파일이 없으면 상태별 색 원으로 대신 표시한다.

#include "ui_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "app_state.h"
#include "sdkconfig.h"

typedef struct {
    lv_obj_t *temp;
    lv_obj_t *cond;
    lv_obj_t *icon;         // 아이콘 폰트가 있으면 label, 없으면 색 원
    lv_obj_t *humidity;
    lv_obj_t *updated;
} weather_view_t;

typedef struct {
    const char *en;
    const char *ko;
    uint32_t    glyph_day;      // Weather Icons 코드 포인트 (tools/make_fonts.py 와 일치해야 함)
    uint32_t    glyph_night;
    uint32_t    color;
} wmo_desc_t;

// WMO weather interpretation codes (Open-Meteo)
static wmo_desc_t wmo_describe(int code)
{
    if (code == 0)                return (wmo_desc_t){ "Clear", "맑음", 0xF00D, 0xF02E, 0xFFB000 };
    if (code <= 2)                return (wmo_desc_t){ "Partly cloudy", "구름 조금", 0xF002, 0xF086, 0xF2C94C };
    if (code == 3)                return (wmo_desc_t){ "Overcast", "흐림", 0xF013, 0xF013, 0xAEB6C2 };
    if (code == 45 || code == 48) return (wmo_desc_t){ "Fog", "안개", 0xF014, 0xF014, 0x9DA5B4 };
    if (code >= 51 && code <= 57) return (wmo_desc_t){ "Drizzle", "이슬비", 0xF01C, 0xF01C, 0x6FB7FF };
    if (code == 66 || code == 67) return (wmo_desc_t){ "Freezing rain", "어는 비", 0xF0B5, 0xF0B5, 0x9FD4FF };
    if (code >= 61 && code <= 65) return (wmo_desc_t){ "Rain", "비", 0xF019, 0xF019, 0x3DA9FC };
    if (code >= 71 && code <= 77) return (wmo_desc_t){ "Snow", "눈", 0xF01B, 0xF01B, 0xE6EDF3 };
    if (code >= 80 && code <= 82) return (wmo_desc_t){ "Showers", "소나기", 0xF01A, 0xF01A, 0x3DA9FC };
    if (code == 85 || code == 86) return (wmo_desc_t){ "Snow showers", "소낙눈", 0xF01B, 0xF01B, 0xE6EDF3 };
    if (code >= 95)               return (wmo_desc_t){ "Thunderstorm", "뇌우", 0xF01E, 0xF01E, 0xB478FF };
    return (wmo_desc_t){ "Unknown", "알 수 없음", 0xF013, 0xF013, 0x8B949E };
}

// 영문 도시 이름 → 한국어 (Kconfig 파일은 ASCII 만 쓸 수 있어 여기서 변환)
static const char *city_name(void)
{
    static const struct { const char *en, *ko; } k_cities[] = {
        { "Seoul", "서울" }, { "Busan", "부산" }, { "Incheon", "인천" }, { "Daegu", "대구" },
        { "Daejeon", "대전" }, { "Gwangju", "광주" }, { "Ulsan", "울산" }, { "Suwon", "수원" },
        { "Sejong", "세종" }, { "Jeju", "제주" },
    };
    if (ui_lang() == APP_LANG_KO) {
        for (size_t i = 0; i < sizeof(k_cities) / sizeof(k_cities[0]); i++) {
            if (strcmp(k_cities[i].en, CONFIG_APP_WEATHER_CITY) == 0) {
                return k_cities[i].ko;
            }
        }
    }
    return CONFIG_APP_WEATHER_CITY;
}

static void utf8_encode(uint32_t cp, char out[5])
{
    if (cp < 0x80) {
        out[0] = (char)cp; out[1] = 0;
    } else if (cp < 0x800) {
        out[0] = (char)(0xC0 | cp >> 6); out[1] = (char)(0x80 | (cp & 0x3F)); out[2] = 0;
    } else {
        out[0] = (char)(0xE0 | cp >> 12); out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F)); out[3] = 0;
    }
}

static void weather_cb(lv_observer_t *o, lv_subject_t *s)
{
    lv_obj_t *root = lv_observer_get_target_obj(o);
    weather_view_t *v = lv_obj_get_user_data(root);
    weather_info_t w;
    app_state_get_weather(&w);

    if (!w.valid) {
        lv_label_set_text(v->temp, "--");
        lv_label_set_text(v->cond, TR("No data yet", "아직 데이터 없음"));
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
    lv_label_set_text(v->cond, TR(d.en, d.ko));
    if (ui_font_weather_l) {
        char glyph[5];
        utf8_encode(w.is_day ? d.glyph_day : d.glyph_night, glyph);
        lv_label_set_text(v->icon, glyph);
        lv_obj_set_style_text_color(v->icon, lv_color_hex(d.color), 0);
    } else {
        lv_obj_set_style_bg_color(v->icon, lv_color_hex(d.color), 0);
    }
    lv_label_set_text_fmt(v->humidity, TR(LV_SYMBOL_TINT "  Humidity %d%%", LV_SYMBOL_TINT "  습도 %d%%"), w.humidity);

    if (v->updated) {
        struct tm tm;
        localtime_r(&w.updated_at, &tm);
        lv_label_set_text_fmt(v->updated, TR("Updated %02d:%02d", "%02d:%02d 갱신"), tm.tm_hour, tm.tm_min);
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
    lv_obj_t *title = ui_card_title(head, LV_SYMBOL_HOME, "");
    lv_label_set_text_fmt(title, "%s  %s  %s", LV_SYMBOL_HOME, TR("Weather", "날씨"), city_name());
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
    lv_obj_set_style_pad_column(row, 20, 0);
    lv_obj_set_style_pad_top(row, full ? 16 : 4, 0);

    const lv_font_t *icon_font = full ? ui_font_weather_l : ui_font_weather_s;
    if (icon_font) {
        v->icon = ui_label(row, icon_font, UI_COLOR_DIM, "");
    } else {
        v->icon = lv_obj_create(row);
        lv_obj_remove_style_all(v->icon);
        lv_obj_set_size(v->icon, full ? 72 : 44, full ? 72 : 44);
        lv_obj_set_style_radius(v->icon, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(v->icon, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(v->icon, UI_COLOR_DIM, 0);
    }

    v->temp = ui_label(row, UI_FONT_XL, UI_COLOR_TEXT, "");
    v->cond = ui_label(parent, full ? UI_FONT_L : UI_FONT_M, UI_COLOR_TEXT, "");
    v->humidity = ui_label(parent, full ? UI_FONT_M : UI_FONT_S, UI_COLOR_DIM, "");
    if (full) {
        v->updated = ui_label(parent, UI_FONT_S, UI_COLOR_DIM, "");
    }

    lv_subject_add_observer_obj(&ui_subj_weather, weather_cb, parent, NULL);
}
