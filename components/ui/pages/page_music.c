// 음악 리모컨: 스마트폰에서 재생 중인 곡 정보 + 재생 제어 (BLE, media_ble 컴포넌트)
// 버튼은 APP_EVT_REQ_MEDIA_CMD 이벤트만 보내고, 실제 전송은 media_ble 이 담당한다.
// 한글 곡명은 5단계에서 한글 폰트(Tiny TTF)를 붙인 뒤 표시된다.

#include "ui_internal.h"

#include "app_state.h"
#include "media_ble.h"

typedef struct {
    lv_obj_t *title;
    lv_obj_t *artist;
    lv_obj_t *album;
    lv_obj_t *progress;
    lv_obj_t *time;
    lv_obj_t *play_label;
    lv_obj_t *hint;
} music_view_t;

static void fmt_mmss(char *buf, size_t size, uint32_t s)
{
    lv_snprintf(buf, size, "%u:%02u", (unsigned)(s / 60), (unsigned)(s % 60));
}

static void media_cb(lv_observer_t *o, lv_subject_t *s)
{
    lv_obj_t *root = lv_observer_get_target_obj(o);
    music_view_t *v = lv_obj_get_user_data(root);
    media_info_t *m = lv_malloc(sizeof(media_info_t));
    if (!m) {
        return;
    }
    app_state_get_media(m);

    lv_label_set_text(v->title, m->title[0] ? m->title : "Nothing playing");
    lv_label_set_text(v->artist, m->artist);
    if (v->album) {
        lv_label_set_text(v->album, m->album);
    }
    lv_label_set_text(v->play_label, m->state == MEDIA_PLAYING ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

    if (v->progress) {
        int32_t pct = m->duration_s ? (int32_t)(m->elapsed_s * 100 / m->duration_s) : 0;
        lv_bar_set_value(v->progress, pct, LV_ANIM_OFF);
        char a[12], b[12];
        fmt_mmss(a, sizeof(a), m->elapsed_s);
        fmt_mmss(b, sizeof(b), m->duration_s);
        lv_label_set_text_fmt(v->time, "%s / %s", a, b);
    }
    lv_free(m);
}

static void ble_cb(lv_observer_t *o, lv_subject_t *s)
{
    lv_obj_t *root = lv_observer_get_target_obj(o);
    music_view_t *v = lv_obj_get_user_data(root);
    int32_t st = lv_subject_get_int(s);
    if (st >= BLE_STATE_CONNECTED) {
        lv_obj_add_flag(v->hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(v->hint, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(v->hint, st == BLE_STATE_ADVERTISING
                          ? LV_SYMBOL_BLUETOOTH "  Pair \"Smart Display\" in your phone's Bluetooth settings"
                          : LV_SYMBOL_BLUETOOTH "  Bluetooth off");
    }
}

static void on_cmd(lv_event_t *e)
{
    media_cmd_t cmd = (media_cmd_t)(intptr_t)lv_event_get_user_data(e);
    app_event_post(APP_EVT_REQ_MEDIA_CMD, &cmd, sizeof(cmd));
}

static lv_obj_t *ctrl_button(lv_obj_t *parent, const char *symbol, media_cmd_t cmd, int32_t size, bool primary)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, size, size);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, primary ? UI_COLOR_ACCENT : UI_COLOR_CARD_ALT, 0);
    lv_obj_add_event_cb(btn, on_cmd, LV_EVENT_CLICKED, (void *)(intptr_t)cmd);
    lv_obj_t *label = ui_label(btn, size >= 72 ? UI_FONT_L : UI_FONT_M, UI_COLOR_TEXT, symbol);
    lv_obj_center(label);
    return label;
}

static lv_obj_t *controls_row(lv_obj_t *parent, music_view_t *v, bool full)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, full ? 28 : 16, 0);

    int32_t small = full ? 64 : 48, big = full ? 88 : 60;
    if (full) {
        ctrl_button(row, LV_SYMBOL_VOLUME_MID, MEDIA_CMD_VOL_DOWN, 52, false);
    }
    ctrl_button(row, LV_SYMBOL_PREV, MEDIA_CMD_PREV, small, false);
    v->play_label = ctrl_button(row, LV_SYMBOL_PLAY, MEDIA_CMD_PLAY_PAUSE, big, true);
    ctrl_button(row, LV_SYMBOL_NEXT, MEDIA_CMD_NEXT, small, false);
    if (full) {
        ctrl_button(row, LV_SYMBOL_VOLUME_MAX, MEDIA_CMD_VOL_UP, 52, false);
    }
    return row;
}

void ui_music_create(lv_obj_t *parent, bool full)
{
    music_view_t *v = lv_malloc_zeroed(sizeof(music_view_t));
    lv_obj_set_user_data(parent, v);
    ui_free_user_data_on_delete(parent);

    if (!full) {
        ui_card_title(parent, LV_SYMBOL_AUDIO, "Music");
    } else {
        lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(parent, 10, 0);

        lv_obj_t *art = lv_obj_create(parent);
        lv_obj_remove_style_all(art);
        lv_obj_set_size(art, 120, 120);
        lv_obj_set_style_radius(art, 20, 0);
        lv_obj_set_style_bg_opa(art, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(art, UI_COLOR_CARD_ALT, 0);
        lv_obj_center(ui_label(art, UI_FONT_XL, UI_COLOR_ACCENT, LV_SYMBOL_AUDIO));
    }

    v->title = ui_label(parent, full ? UI_FONT_L : UI_FONT_M, UI_COLOR_TEXT, "");
    lv_obj_set_width(v->title, LV_PCT(100));
    lv_label_set_long_mode(v->title, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
    v->artist = ui_label(parent, full ? UI_FONT_M : UI_FONT_S, UI_COLOR_DIM, "");
    if (full) {
        lv_obj_set_style_text_align(v->title, LV_TEXT_ALIGN_CENTER, 0);
        v->album = ui_label(parent, UI_FONT_S, UI_COLOR_DIM, "");

        v->progress = lv_bar_create(parent);
        lv_obj_set_size(v->progress, LV_PCT(80), 6);
        lv_bar_set_range(v->progress, 0, 100);
        v->time = ui_label(parent, UI_FONT_S, UI_COLOR_DIM, "");
    }

    lv_obj_t *row = controls_row(parent, v, full);
    if (!full) {
        lv_obj_set_style_pad_top(row, 6, 0);
    }

    v->hint = ui_label(parent, UI_FONT_S, UI_COLOR_WARM, "");
    lv_obj_set_width(v->hint, LV_PCT(100));
    lv_label_set_long_mode(v->hint, LV_LABEL_LONG_MODE_WRAP);
    if (full) {
        lv_obj_set_style_text_align(v->hint, LV_TEXT_ALIGN_CENTER, 0);
    }

    lv_subject_add_observer_obj(&ui_subj_media, media_cb, parent, NULL);
    lv_subject_add_observer_obj(&ui_subj_ble, ble_cb, parent, NULL);
}
