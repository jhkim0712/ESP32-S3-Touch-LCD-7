// 시계: 디지털(시:분:초 + 날짜) 과 아날로그(lv_scale + 바늘 3개)
//   카드(compact): 디지털만
//   슬라이드(full): 아날로그 + 디지털 (가로는 좌우, 세로는 위아래 배치)

#include "ui_internal.h"

#include <time.h>

typedef struct {
    lv_obj_t *hour;
    lv_obj_t *minute;
    lv_obj_t *second;
    int32_t   radius;
} analog_t;

static bool local_time(int32_t epoch, struct tm *tm)
{
    time_t t = epoch;
    localtime_r(&t, tm);
    return tm->tm_year + 1900 >= 2025;    // SNTP 동기화 전에는 1970년
}

static void time_label_cb(lv_observer_t *o, lv_subject_t *s)
{
    struct tm tm;
    lv_obj_t *label = lv_observer_get_target_obj(o);
    bool with_seconds = (bool)(intptr_t)lv_observer_get_user_data(o);
    if (!local_time(lv_subject_get_int(s), &tm)) {
        lv_label_set_text(label, with_seconds ? "--:--:--" : "--:--");
        return;
    }
    char buf[16];
    strftime(buf, sizeof(buf), with_seconds ? "%H:%M:%S" : "%H:%M", &tm);
    lv_label_set_text(label, buf);
}

static void date_label_cb(lv_observer_t *o, lv_subject_t *s)
{
    struct tm tm;
    lv_obj_t *label = lv_observer_get_target_obj(o);
    if (!local_time(lv_subject_get_int(s), &tm)) {
        lv_label_set_text(label, TR("Waiting for NTP sync", "인터넷 시간 동기화 대기 중"));
        return;
    }
    char buf[48];
    ui_fmt_date(buf, sizeof(buf), &tm, true);
    lv_label_set_text(label, buf);
}

static void analog_cb(lv_observer_t *o, lv_subject_t *s)
{
    struct tm tm;
    lv_obj_t *scale = lv_observer_get_target_obj(o);
    analog_t *a = lv_obj_get_user_data(scale);
    if (!a || !local_time(lv_subject_get_int(s), &tm)) {
        return;
    }
    // 눈금 범위 0..60 (한 바퀴). 시침은 분에 따라 조금씩 움직인다.
    int32_t hour_value = (tm.tm_hour % 12) * 5 + tm.tm_min / 12;
    lv_scale_set_line_needle_value(scale, a->hour, a->radius * 50 / 100, hour_value);
    lv_scale_set_line_needle_value(scale, a->minute, a->radius * 75 / 100, tm.tm_min);
    lv_scale_set_line_needle_value(scale, a->second, a->radius * 85 / 100, tm.tm_sec);
}

static lv_obj_t *make_needle(lv_obj_t *scale, lv_color_t color, int32_t width)
{
    lv_obj_t *line = lv_line_create(scale);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_color(line, color, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    return line;
}

static void analog_create(lv_obj_t *parent, int32_t size)
{
    static const char *numbers[] = { "12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "", NULL };

    lv_obj_t *scale = lv_scale_create(parent);
    lv_obj_set_size(scale, size, size);
    lv_scale_set_mode(scale, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_range(scale, 0, 60);
    lv_scale_set_total_tick_count(scale, 61);
    lv_scale_set_major_tick_every(scale, 5);
    lv_scale_set_angle_range(scale, 360);
    lv_scale_set_rotation(scale, 270);
    lv_scale_set_label_show(scale, true);
    lv_scale_set_text_src(scale, numbers);

    lv_obj_set_style_bg_opa(scale, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(scale, UI_COLOR_CARD, 0);
    lv_obj_set_style_radius(scale, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(scale, 12, 0);
    lv_obj_set_style_text_font(scale, UI_FONT_M, 0);
    lv_obj_set_style_text_color(scale, UI_COLOR_TEXT, 0);
    lv_obj_set_style_length(scale, 6, LV_PART_ITEMS);
    lv_obj_set_style_line_color(scale, UI_COLOR_DIM, LV_PART_ITEMS);
    lv_obj_set_style_length(scale, 14, LV_PART_INDICATOR);
    lv_obj_set_style_line_width(scale, 3, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(scale, UI_COLOR_TEXT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(scale, 0, LV_PART_MAIN);

    analog_t *a = lv_malloc_zeroed(sizeof(analog_t));
    a->radius = size / 2 - 12;
    a->hour = make_needle(scale, UI_COLOR_TEXT, 8);
    a->minute = make_needle(scale, UI_COLOR_TEXT, 5);
    a->second = make_needle(scale, UI_COLOR_WARM, 2);
    lv_obj_set_user_data(scale, a);
    ui_free_user_data_on_delete(scale);

    // 중심 캡
    lv_obj_t *cap = lv_obj_create(scale);
    lv_obj_remove_style_all(cap);
    lv_obj_set_size(cap, 14, 14);
    lv_obj_set_style_radius(cap, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(cap, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cap, UI_COLOR_WARM, 0);
    lv_obj_center(cap);

    lv_subject_add_observer_obj(&ui_subj_time, analog_cb, scale, NULL);
}

void ui_clock_create(lv_obj_t *parent, bool full)
{
    if (!full) {
        ui_card_title(parent, LV_SYMBOL_BELL, TR("Clock  (KST)", "시계  (한국 표준시)"));
        lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_t *t = ui_label(parent, UI_FONT_XL, UI_COLOR_TEXT, "");
        lv_obj_set_style_pad_top(t, 12, 0);
        lv_subject_add_observer_obj(&ui_subj_time, time_label_cb, t, (void *)(intptr_t)true);
        lv_obj_t *d = ui_label(parent, UI_FONT_M, UI_COLOR_DIM, "");
        lv_subject_add_observer_obj(&ui_subj_time, date_label_cb, d, NULL);
        return;
    }

    bool portrait = ui_is_portrait();
    lv_obj_set_flex_flow(parent, portrait ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(parent, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // 세로: 화면 폭 - 좌우 화살표 여백, 가로: 콘텐츠 높이 - 페이지 점 여백
    int32_t size = portrait ? lv_display_get_horizontal_resolution(lv_display_get_default()) - 160
                            : ui_content_height() - 60;
    analog_create(parent, size);

    lv_obj_t *col = lv_obj_create(parent);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(col, 8, 0);

    lv_obj_t *t = ui_label(col, UI_FONT_XL, UI_COLOR_TEXT, "");
    lv_subject_add_observer_obj(&ui_subj_time, time_label_cb, t, (void *)(intptr_t)true);
    lv_obj_t *d = ui_label(col, UI_FONT_M, UI_COLOR_DIM, "");
    lv_subject_add_observer_obj(&ui_subj_time, date_label_cb, d, NULL);
    ui_label(col, UI_FONT_S, UI_COLOR_DIM, TR("KST (UTC+9)", "한국 표준시 (UTC+9)"));
}
