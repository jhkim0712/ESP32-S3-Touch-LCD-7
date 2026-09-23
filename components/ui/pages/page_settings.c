// 설정 페이지 (상태 표시줄의 톱니바퀴 버튼)
//   Wi-Fi SSID/비밀번호, 화면 회전, 사진 전환 간격, 주식 종목, 기기 정보
// [Save] → NVS 저장 + APP_EVT_SETTINGS_CHANGED. 회전이 바뀌었으면 재부팅을 묻는다.
// Wi-Fi 목록 스캔은 Wi-Fi 연결 기능(5-2)과 함께 추가한다.

#include "ui_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_state.h"
#include "board.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_system.h"

typedef struct {
    lv_obj_t *ssid;
    lv_obj_t *pass;
    lv_obj_t *rotation;
    lv_obj_t *photo;
    lv_obj_t *symbols;
    lv_obj_t *kb;
    lv_obj_t *status;
} settings_view_t;

static const int  k_intervals[] = { 5, 10, 30, 60, 300 };
static const char k_interval_opts[] = "5 s\n10 s\n30 s\n1 min\n5 min";
static const char k_rotation_opts[] = "0\xC2\xB0\n90\xC2\xB0\n180\xC2\xB0\n270\xC2\xB0";

static lv_obj_t *s_screen;

bool ui_settings_is_open(void)
{
    return s_screen != NULL;
}

static int interval_index(int seconds)
{
    int best = 0;
    for (int i = 0; i < (int)(sizeof(k_intervals) / sizeof(k_intervals[0])); i++) {
        if (abs(k_intervals[i] - seconds) < abs(k_intervals[best] - seconds)) {
            best = i;
        }
    }
    return best;
}

// ---- 키보드 ----

static void on_ta_focus(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    settings_view_t *v = lv_event_get_user_data(e);
    if (lv_event_get_code(e) == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(v->kb, ta);
        lv_obj_remove_flag(v->kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_scroll_to_view_recursive(ta, LV_ANIM_ON);
    } else {
        lv_obj_add_flag(v->kb, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_kb_done(lv_event_t *e)
{
    lv_obj_t *kb = lv_event_get_target(e);
    lv_obj_t *ta = lv_keyboard_get_textarea(kb);
    if (ta) {
        lv_obj_remove_state(ta, LV_STATE_FOCUSED);
    }
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
}

// ---- 레이아웃 헬퍼 ----

static lv_obj_t *section(lv_obj_t *parent, const char *symbol, const char *title)
{
    lv_obj_t *label = ui_label(parent, UI_FONT_S, UI_COLOR_ACCENT, "");
    lv_label_set_text_fmt(label, "%s  %s", symbol, title);
    lv_obj_set_style_pad_top(label, 8, 0);

    lv_obj_t *card = ui_card_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(card, 10, 0);
    return card;
}

static lv_obj_t *row(lv_obj_t *card, const char *name)
{
    lv_obj_t *r = lv_obj_create(card);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(r, 12, 0);
    lv_obj_t *label = ui_label(r, UI_FONT_M, UI_COLOR_TEXT, name);
    lv_obj_set_width(label, 150);
    return r;
}

static lv_obj_t *text_field(lv_obj_t *parent, settings_view_t *v, const char *text, uint32_t max_len, bool password)
{
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, max_len);
    lv_textarea_set_password_mode(ta, password);
    lv_textarea_set_text(ta, text);
    lv_obj_set_flex_grow(ta, 1);
    lv_obj_add_event_cb(ta, on_ta_focus, LV_EVENT_FOCUSED, v);
    lv_obj_add_event_cb(ta, on_ta_focus, LV_EVENT_DEFOCUSED, v);
    return ta;
}

static lv_obj_t *hint(lv_obj_t *card, const char *text)
{
    lv_obj_t *label = ui_label(card, UI_FONT_S, UI_COLOR_DIM, text);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_WRAP);
    return label;
}

static lv_obj_t *dropdown(lv_obj_t *parent, const char *options, uint32_t selected)
{
    lv_obj_t *dd = lv_dropdown_create(parent);
    lv_dropdown_set_options_static(dd, options);
    lv_dropdown_set_selected(dd, selected);
    lv_obj_set_width(dd, 180);
    return dd;
}

// ---- 동작 ----

static void on_back(lv_event_t *e)
{
    ui_show_home(LV_SCREEN_LOAD_ANIM_MOVE_RIGHT);
}

static void on_restart(lv_event_t *e)
{
    board_backlight_set(false);
    esp_restart();
}

static void on_close_msgbox(lv_event_t *e)
{
    lv_msgbox_close(lv_event_get_user_data(e));
}

static void on_save(lv_event_t *e)
{
    settings_view_t *v = lv_event_get_user_data(e);
    app_settings_t s;
    settings_load(&s);   // ui_mode 등 이 페이지에 없는 값 유지

    strlcpy(s.wifi_ssid, lv_textarea_get_text(v->ssid), sizeof(s.wifi_ssid));
    strlcpy(s.wifi_pass, lv_textarea_get_text(v->pass), sizeof(s.wifi_pass));
    strlcpy(s.stock_symbols, lv_textarea_get_text(v->symbols), sizeof(s.stock_symbols));
    s.rotation = (ui_rotation_t)lv_dropdown_get_selected(v->rotation);
    s.photo_interval_s = k_intervals[lv_dropdown_get_selected(v->photo)];

    if (settings_save(&s) != ESP_OK) {
        lv_label_set_text(v->status, LV_SYMBOL_WARNING "  Save failed");
        return;
    }
    lv_label_set_text(v->status, LV_SYMBOL_OK "  Saved");
    app_event_post(APP_EVT_SETTINGS_CHANGED, NULL, 0);

    if (s.rotation != ui_active_rotation()) {
        lv_obj_t *mb = lv_msgbox_create(NULL);
        lv_msgbox_add_title(mb, LV_SYMBOL_REFRESH "  Restart required");
        lv_msgbox_add_text(mb, "The new screen rotation is applied after a restart.");
        lv_obj_add_event_cb(lv_msgbox_add_footer_button(mb, "Later"), on_close_msgbox, LV_EVENT_CLICKED, mb);
        lv_obj_add_event_cb(lv_msgbox_add_footer_button(mb, "Restart"), on_restart, LV_EVENT_CLICKED, NULL);
    }
}

static void on_screen_deleted(lv_event_t *e)
{
    s_screen = NULL;
}

void ui_settings_open(void)
{
    if (s_screen) {
        return;
    }
    app_settings_t s;
    settings_load(&s);

    settings_view_t *v = lv_malloc_zeroed(sizeof(settings_view_t));
    lv_obj_t *scr = ui_screen_create();
    s_screen = scr;
    lv_obj_set_user_data(scr, v);
    ui_free_user_data_on_delete(scr);
    lv_obj_add_event_cb(scr, on_screen_deleted, LV_EVENT_DELETE, NULL);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_top(scr, UI_STATUS_BAR_H, 0);

    // 헤더: [Back]  Settings  (상태)  [Save]
    lv_obj_t *head = lv_obj_create(scr);
    lv_obj_remove_style_all(head);
    lv_obj_set_size(head, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(head, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(head, UI_GAP, 0);
    lv_obj_set_style_pad_column(head, 16, 0);

    lv_obj_t *back = lv_button_create(head);
    lv_obj_set_style_bg_color(back, UI_COLOR_CARD_ALT, 0);
    lv_obj_add_event_cb(back, on_back, LV_EVENT_CLICKED, NULL);
    lv_obj_center(ui_label(back, UI_FONT_M, UI_COLOR_TEXT, LV_SYMBOL_LEFT "  Back"));

    ui_label(head, UI_FONT_L, UI_COLOR_TEXT, "Settings");
    v->status = ui_label(head, UI_FONT_S, UI_COLOR_DIM, "");
    lv_obj_set_flex_grow(v->status, 1);
    lv_obj_set_style_text_align(v->status, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *save = lv_button_create(head);
    lv_obj_add_event_cb(save, on_save, LV_EVENT_CLICKED, v);
    lv_obj_center(ui_label(save, UI_FONT_M, UI_COLOR_TEXT, LV_SYMBOL_SAVE "  Save"));

    // 스크롤 영역
    lv_obj_t *body = lv_obj_create(scr);
    lv_obj_remove_style_all(body);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_flex_grow(body, 1);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(body, UI_GAP, 0);
    lv_obj_set_style_pad_row(body, 6, 0);
    lv_obj_add_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);

    lv_obj_t *card = section(body, LV_SYMBOL_WIFI, "Wi-Fi");
    v->ssid = text_field(row(card, "SSID"), v, s.wifi_ssid, sizeof(s.wifi_ssid) - 1, false);
    v->pass = text_field(row(card, "Password"), v, s.wifi_pass, sizeof(s.wifi_pass) - 1, true);

    card = section(body, LV_SYMBOL_IMAGE, "Display");
    v->rotation = dropdown(row(card, "Rotation"), k_rotation_opts, s.rotation);
    hint(card, "Rotation is applied after a restart. 90/180/270 use software rotation.");

    card = section(body, LV_SYMBOL_IMAGE, "Photo frame");
    v->photo = dropdown(row(card, "Interval"), k_interval_opts, interval_index(s.photo_interval_s));

    card = section(body, LV_SYMBOL_SHUFFLE, "Stocks");
    v->symbols = text_field(row(card, "Symbols"), v, s.stock_symbols, sizeof(s.stock_symbols) - 1, false);
    hint(card, "Comma separated Yahoo symbols. ^KS11 = KOSPI, 005930.KS = Samsung");

    card = section(body, LV_SYMBOL_LIST, "About");
    const esp_app_desc_t *app = esp_app_get_description();
    lv_obj_t *about = hint(card, "");
    lv_label_set_text_fmt(about, "Firmware v%s  (ESP-IDF %s)\nFree heap: internal %u KB, PSRAM %u KB",
                          app->version, app->idf_ver,
                          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));

    // 화면 키보드 (텍스트 입력칸을 누르면 표시)
    v->kb = lv_keyboard_create(scr);
    lv_obj_set_size(v->kb, LV_PCT(100), LV_PCT(40));
    lv_obj_add_flag(v->kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(v->kb, on_kb_done, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(v->kb, on_kb_done, LV_EVENT_CANCEL, NULL);

    lv_screen_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_LEFT, 250, 0, true);
}
