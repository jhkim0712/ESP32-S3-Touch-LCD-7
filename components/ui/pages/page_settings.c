// 설정 페이지 (상태 표시줄의 톱니바퀴 버튼)
//   Wi-Fi SSID(직접 입력 또는 [Scan] 목록에서 선택)/비밀번호, 연결 상태, BLE 페어링 상태/해제,
//   화면 회전, 사진 전환 간격, 주식 종목, 웹 설정 주소/PIN, 기기 정보
// [Save] → NVS 저장 + APP_EVT_SETTINGS_CHANGED (Wi-Fi 는 새 설정으로 재연결).
// 회전이 바뀌었으면 재부팅을 묻는다.

#include "ui.h"
#include "ui_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_state.h"
#include "board.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "media_ble.h"
#include "sdkconfig.h"

#define WEB_HOSTNAME    "smart-display"   // wifi_mgr 의 호스트 이름, mDNS 이름

typedef struct {
    lv_obj_t *ssid;
    lv_obj_t *pass;
    lv_obj_t *wifi_status;
    lv_obj_t *rotation;
    lv_obj_t *photo;
    lv_obj_t *symbols;
    lv_obj_t *language;
    lv_obj_t *kb;
    lv_obj_t *status;
} settings_view_t;

static const int  k_intervals[] = { 5, 10, 30, 60, 300 };
static const char k_interval_opts_en[] = "5 s\n10 s\n30 s\n1 min\n5 min";
static const char k_interval_opts_ko[] = "5초\n10초\n30초\n1분\n5분";
static const char k_language_opts[] = "English\n한국어";   // app_lang_t 순서
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

// ---- Wi-Fi 스캔 목록 ----

typedef struct {
    settings_view_t *view;
    int32_t          start_count;   // 열었을 때의 스캔 카운터 (이전 결과를 보여주지 않기 위함)
} scan_dialog_t;

static void on_ap_selected(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_current_target(e);
    lv_obj_t *mbox = lv_event_get_user_data(e);
    scan_dialog_t *d = lv_obj_get_user_data(mbox);
    settings_view_t *v = d->view;

    lv_textarea_set_text(v->ssid, lv_label_get_text(lv_obj_get_child(btn, 0)));
    lv_textarea_set_text(v->pass, "");
    lv_msgbox_close(mbox);

    // 비밀번호 칸으로 이동해 키보드를 띄운다
    lv_obj_add_state(v->pass, LV_STATE_FOCUSED);
    lv_obj_send_event(v->pass, LV_EVENT_FOCUSED, NULL);
}

static const char *signal_text(int8_t rssi)
{
    return rssi >= -55 ? TR("Excellent", "매우 좋음") : rssi >= -67 ? TR("Good", "좋음")
         : rssi >= -75 ? TR("Fair", "보통") : TR("Weak", "약함");
}

static void scan_result_cb(lv_observer_t *o, lv_subject_t *subj)
{
    lv_obj_t *mbox = lv_observer_get_target_obj(o);
    scan_dialog_t *d = lv_obj_get_user_data(mbox);
    if (lv_subject_get_int(subj) == d->start_count) {
        return;   // 아직 새 결과 없음 (스피너 유지)
    }

    wifi_scan_t *scan = lv_malloc(sizeof(wifi_scan_t));
    if (!scan) {
        return;
    }
    app_state_get_wifi_scan(scan);

    lv_obj_t *content = lv_msgbox_get_content(mbox);
    lv_obj_clean(content);
    if (scan->count == 0) {
        ui_label(content, UI_FONT_M, UI_COLOR_DIM, TR("No networks found", "찾은 네트워크가 없습니다"));
    }
    for (int i = 0; i < scan->count; i++) {
        const wifi_ap_t *ap = &scan->aps[i];
        lv_obj_t *btn = lv_button_create(content);
        lv_obj_set_width(btn, LV_PCT(100));
        lv_obj_set_style_bg_color(btn, UI_COLOR_CARD_ALT, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_add_event_cb(btn, on_ap_selected, LV_EVENT_CLICKED, mbox);

        lv_obj_t *name = ui_label(btn, UI_FONT_M, UI_COLOR_TEXT, ap->ssid);   // child 0 = SSID
        lv_obj_set_flex_grow(name, 1);
        lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
        lv_obj_t *info = ui_label(btn, UI_FONT_S, UI_COLOR_DIM, "");
        lv_label_set_text_fmt(info, "%s%s  %d dBm", ap->secure ? LV_SYMBOL_EYE_CLOSE "  " : "",
                              signal_text(ap->rssi), ap->rssi);
    }
    lv_free(scan);
}

static void on_scan_close(lv_event_t *e)
{
    lv_msgbox_close(lv_event_get_user_data(e));
}

static lv_obj_t *s_scan_mbox;   // 설정 화면이 닫히면 함께 닫는다 (view 포인터 보호)

static void start_scan(lv_obj_t *mbox)
{
    scan_dialog_t *d = lv_obj_get_user_data(mbox);
    d->start_count = lv_subject_get_int(&ui_subj_wifi_scan);
    lv_obj_t *content = lv_msgbox_get_content(mbox);
    lv_obj_clean(content);
    lv_obj_t *spinner = lv_spinner_create(content);
    lv_obj_set_size(spinner, 48, 48);
    ui_label(content, UI_FONT_M, UI_COLOR_DIM, TR("Scanning...", "검색 중..."));
    app_event_post(APP_EVT_REQ_WIFI_SCAN, NULL, 0);
}

static void on_scan_rescan(lv_event_t *e)
{
    start_scan(lv_event_get_user_data(e));
}

static void on_scan_mbox_deleted(lv_event_t *e)
{
    s_scan_mbox = NULL;
}

static void on_scan(lv_event_t *e)
{
    settings_view_t *v = lv_event_get_user_data(e);
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_obj_set_size(mbox, ui_is_portrait() ? LV_PCT(92) : 560, LV_PCT(80));
    lv_msgbox_add_title(mbox, TR(LV_SYMBOL_WIFI "  Wi-Fi networks", LV_SYMBOL_WIFI "  Wi-Fi 네트워크"));

    scan_dialog_t *d = lv_malloc_zeroed(sizeof(scan_dialog_t));
    d->view = v;
    lv_obj_set_user_data(mbox, d);
    ui_free_user_data_on_delete(mbox);
    s_scan_mbox = mbox;
    lv_obj_add_event_cb(mbox, on_scan_mbox_deleted, LV_EVENT_DELETE, NULL);

    lv_obj_t *content = lv_msgbox_get_content(mbox);
    lv_obj_set_flex_grow(content, 1);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(content, 6, 0);

    lv_obj_add_event_cb(lv_msgbox_add_footer_button(mbox, TR(LV_SYMBOL_REFRESH "  Rescan", LV_SYMBOL_REFRESH "  다시 검색")), on_scan_rescan,
                        LV_EVENT_CLICKED, mbox);
    lv_obj_add_event_cb(lv_msgbox_add_footer_button(mbox, TR("Cancel", "취소")), on_scan_close, LV_EVENT_CLICKED, mbox);

    start_scan(mbox);   // 스피너 표시 + 스캔 요청
    lv_subject_add_observer_obj(&ui_subj_wifi_scan, scan_result_cb, mbox, NULL);
}

static void wifi_status_cb(lv_observer_t *o, lv_subject_t *subj)
{
    lv_obj_t *label = lv_observer_get_target_obj(o);
    char ip[16];
    app_state_get_ip(ip, sizeof(ip));
    if (lv_subject_get_int(subj) && ip[0]) {
        lv_label_set_text_fmt(label, TR(LV_SYMBOL_OK "  Connected  -  IP %s", LV_SYMBOL_OK "  연결됨  -  IP %s"), ip);
        lv_obj_set_style_text_color(label, UI_COLOR_ACCENT, 0);
    } else {
        lv_label_set_text(label, TR("Not connected", "연결 안 됨"));
        lv_obj_set_style_text_color(label, UI_COLOR_DIM, 0);
    }
}

static void web_address_cb(lv_observer_t *o, lv_subject_t *subj)
{
    lv_obj_t *label = lv_observer_get_target_obj(o);
    char ip[16];
    app_state_get_ip(ip, sizeof(ip));
    if (lv_subject_get_int(subj) && ip[0]) {
        lv_label_set_text_fmt(label, "http://" WEB_HOSTNAME ".local\nhttp://%s", ip);
        lv_obj_set_style_text_color(label, UI_COLOR_TEXT, 0);
    } else {
        lv_label_set_text(label, TR("Available after Wi-Fi connects", "Wi-Fi 에 연결되면 사용할 수 있습니다"));
        lv_obj_set_style_text_color(label, UI_COLOR_DIM, 0);
    }
}

static void __attribute__((unused)) ble_status_cb(lv_observer_t *o, lv_subject_t *subj)
{
    lv_obj_t *label = lv_observer_get_target_obj(o);
    switch (lv_subject_get_int(subj)) {
    case BLE_STATE_BONDED:
    case BLE_STATE_CONNECTED:
        lv_label_set_text(label, TR(LV_SYMBOL_OK "  Phone connected", LV_SYMBOL_OK "  휴대폰 연결됨"));
        lv_obj_set_style_text_color(label, UI_COLOR_ACCENT, 0);
        break;
    case BLE_STATE_ADVERTISING:
        lv_label_set_text(label, TR("Waiting for a phone - pair \"Smart Display\" in the phone's Bluetooth settings",
                                    "휴대폰 대기 중 - 휴대폰 블루투스 설정에서 \"Smart Display\"를 연결하세요"));
        lv_obj_set_style_text_color(label, UI_COLOR_WARM, 0);
        break;
    default:
        lv_label_set_text(label, TR("Bluetooth off", "블루투스 꺼짐"));
        lv_obj_set_style_text_color(label, UI_COLOR_DIM, 0);
        break;
    }
}

static void __attribute__((unused)) on_unpair(lv_event_t *e)
{
    app_event_post(APP_EVT_REQ_BLE_UNPAIR, NULL, 0);
    settings_view_t *v = lv_event_get_user_data(e);
    lv_label_set_text(v->status, TR(LV_SYMBOL_OK "  Pairing removed", LV_SYMBOL_OK "  페어링 삭제됨"));
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
    s.language = (app_lang_t)lv_dropdown_get_selected(v->language);

    if (settings_save(&s) != ESP_OK) {
        lv_label_set_text(v->status, TR(LV_SYMBOL_WARNING "  Save failed", LV_SYMBOL_WARNING "  저장 실패"));
        return;
    }
    lv_label_set_text(v->status, TR(LV_SYMBOL_OK "  Saved", LV_SYMBOL_OK "  저장됨"));
    app_event_post(APP_EVT_SETTINGS_CHANGED, NULL, 0);

    if (s.language != ui_lang()) {
        // 언어는 바로 적용: 홈 화면을 새 언어로 다시 만들어 돌아간다 (설정 화면은 닫힘)
        ui_set_language(s.language);
        if (s.rotation == ui_active_rotation()) {
            return;
        }
    }

    if (s.rotation != ui_active_rotation()) {
        lv_obj_t *mb = lv_msgbox_create(NULL);
        lv_msgbox_add_title(mb, TR(LV_SYMBOL_REFRESH "  Restart required", LV_SYMBOL_REFRESH "  다시 시작 필요"));
        lv_msgbox_add_text(mb, TR("The new screen rotation is applied after a restart.",
                                  "화면 회전은 다시 시작한 뒤 적용됩니다."));
        lv_obj_add_event_cb(lv_msgbox_add_footer_button(mb, TR("Later", "나중에")), on_close_msgbox, LV_EVENT_CLICKED, mb);
        lv_obj_add_event_cb(lv_msgbox_add_footer_button(mb, TR("Restart", "다시 시작")), on_restart, LV_EVENT_CLICKED, NULL);
    }
}

static void on_screen_deleted(lv_event_t *e)
{
    s_screen = NULL;
    if (s_scan_mbox) {
        lv_msgbox_close(s_scan_mbox);   // 모달 배경까지 함께 삭제
    }
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
    lv_obj_center(ui_label(back, UI_FONT_M, UI_COLOR_TEXT, TR(LV_SYMBOL_LEFT "  Back", LV_SYMBOL_LEFT "  뒤로")));

    ui_label(head, UI_FONT_L, UI_COLOR_TEXT, TR("Settings", "설정"));
    v->status = ui_label(head, UI_FONT_S, UI_COLOR_DIM, "");
    lv_obj_set_flex_grow(v->status, 1);
    lv_obj_set_style_text_align(v->status, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *save = lv_button_create(head);
    lv_obj_add_event_cb(save, on_save, LV_EVENT_CLICKED, v);
    lv_obj_center(ui_label(save, UI_FONT_M, UI_COLOR_TEXT, TR(LV_SYMBOL_SAVE "  Save", LV_SYMBOL_SAVE "  저장")));

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
    lv_obj_t *ssid_row = row(card, TR("SSID", "네트워크 이름"));
    v->ssid = text_field(ssid_row, v, s.wifi_ssid, sizeof(s.wifi_ssid) - 1, false);
    lv_obj_t *scan = lv_button_create(ssid_row);
    lv_obj_set_style_bg_color(scan, UI_COLOR_CARD_ALT, 0);
    lv_obj_add_event_cb(scan, on_scan, LV_EVENT_CLICKED, v);
    lv_obj_center(ui_label(scan, UI_FONT_M, UI_COLOR_TEXT, TR(LV_SYMBOL_WIFI "  Scan", LV_SYMBOL_WIFI "  검색")));
    v->pass = text_field(row(card, TR("Password", "비밀번호")), v, s.wifi_pass, sizeof(s.wifi_pass) - 1, true);
    v->wifi_status = hint(card, "");
    lv_subject_add_observer_obj(&ui_subj_wifi, wifi_status_cb, v->wifi_status, NULL);

#if CONFIG_APP_BLE_MEDIA
    card = section(body, LV_SYMBOL_BLUETOOTH, TR("Bluetooth (music remote)", "블루투스 (음악 리모컨)"));
    lv_obj_t *ble_status = hint(card, "");
    lv_subject_add_observer_obj(&ui_subj_ble, ble_status_cb, ble_status, NULL);
    lv_obj_t *unpair = lv_button_create(card);
    lv_obj_set_style_bg_color(unpair, UI_COLOR_CARD_ALT, 0);
    lv_obj_add_event_cb(unpair, on_unpair, LV_EVENT_CLICKED, v);
    lv_obj_center(ui_label(unpair, UI_FONT_M, UI_COLOR_TEXT,
                           TR(LV_SYMBOL_CLOSE "  Forget paired phones", LV_SYMBOL_CLOSE "  페어링한 휴대폰 삭제")));
    hint(card, TR("iPhone: track info + control (AMS). Android: play/pause/next/previous/volume keys only.",
                  "아이폰: 곡 정보 + 제어 (AMS). 안드로이드: 재생/일시정지/이전/다음/볼륨 키만 가능."));

#endif

    card = section(body, LV_SYMBOL_IMAGE, TR("Display", "화면"));
    v->language = dropdown(row(card, TR("Language", "언어")), k_language_opts, s.language);
    v->rotation = dropdown(row(card, TR("Rotation", "회전")), k_rotation_opts, s.rotation);
    hint(card, TR("Rotation is applied after a restart. 90/180/270 use software rotation.",
                  "회전은 다시 시작한 뒤 적용됩니다. 90/180/270도는 소프트웨어로 회전합니다."));

    card = section(body, LV_SYMBOL_IMAGE, TR("Photo frame", "전자앨범"));
    v->photo = dropdown(row(card, TR("Interval", "전환 간격")), TR(k_interval_opts_en, k_interval_opts_ko),
                        interval_index(s.photo_interval_s));

    card = section(body, LV_SYMBOL_SHUFFLE, TR("Stocks", "주식"));
    v->symbols = text_field(row(card, TR("Symbols", "종목")), v, s.stock_symbols, sizeof(s.stock_symbols) - 1, false);
    hint(card, TR("Comma separated Yahoo symbols. ^KS11 = KOSPI, 005930.KS = Samsung",
                  "쉼표로 구분한 Yahoo 종목 코드. ^KS11 = 코스피, 005930.KS = 삼성전자"));

    card = section(body, LV_SYMBOL_HOME, TR("Web settings", "웹 설정"));
    lv_obj_t *addr = ui_label(row(card, TR("Address", "주소")), UI_FONT_M, UI_COLOR_TEXT, "");
    lv_obj_set_flex_grow(addr, 1);
    lv_subject_add_observer_obj(&ui_subj_wifi, web_address_cb, addr, NULL);
    char pin[8];
    if (settings_get_web_pin(pin, sizeof(pin)) != ESP_OK) {
        strlcpy(pin, "-", sizeof(pin));
    }
    lv_obj_t *pin_label = ui_label(row(card, "PIN"), UI_FONT_L, UI_COLOR_ACCENT, pin);
    lv_obj_set_style_text_letter_space(pin_label, 4, 0);
    hint(card, TR("Open the address in a browser on the same Wi-Fi network and enter the PIN.",
                  "같은 Wi-Fi 에 연결된 PC/휴대폰 브라우저에서 주소를 열고 PIN 을 입력하세요."));

    card = section(body, LV_SYMBOL_LIST, TR("About", "기기 정보"));
    const esp_app_desc_t *app = esp_app_get_description();
    lv_obj_t *about = hint(card, "");
    lv_label_set_text_fmt(about, TR("Firmware v%s  (ESP-IDF %s)\nFree heap: internal %u KB, PSRAM %u KB",
                                    "펌웨어 v%s  (ESP-IDF %s)\n남은 메모리: 내부 %u KB, PSRAM %u KB"),
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
