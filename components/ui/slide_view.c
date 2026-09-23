// 슬라이드형 화면: lv_tileview 가로 스와이프
//   시계 → 전자앨범 → 날씨/주가 (→ 음악: BLE 음악 리모컨을 켠 경우)
// 좌우 스와이프 외에 양쪽 화살표 버튼과 하단 페이지 점으로도 이동한다.

#include "ui_internal.h"

#include "sdkconfig.h"

#define ARROW_SIZE  56

static void page_weather_stocks(lv_obj_t *tile)
{
    lv_obj_set_flex_flow(tile, ui_is_portrait() ? LV_FLEX_FLOW_COLUMN : LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_row(tile, UI_GAP, 0);
    lv_obj_set_style_pad_column(tile, UI_GAP, 0);

    lv_obj_t *w = ui_card_create(tile);
    lv_obj_set_flex_grow(w, 1);
    lv_obj_set_size(w, LV_PCT(100), LV_PCT(100));
    ui_weather_create(w, true);

    lv_obj_t *s = ui_card_create(tile);
    lv_obj_set_flex_grow(s, 1);
    lv_obj_set_size(s, LV_PCT(100), LV_PCT(100));
    ui_stocks_create(s, true);
}

static void page_clock(lv_obj_t *tile)
{
    ui_clock_create(tile, true);
}

static void page_photo(lv_obj_t *tile)
{
    lv_obj_set_style_pad_all(tile, 0, 0);   // 사진은 화면 전체 사용
    ui_photo_create(tile);
}

#if CONFIG_APP_BLE_MEDIA
static void page_music(lv_obj_t *tile)
{
    ui_music_create(tile, true);
}
#endif

static void (*const k_pages[])(lv_obj_t *tile) = {
    page_clock,
    page_photo,
    page_weather_stocks,
#if CONFIG_APP_BLE_MEDIA
    page_music,
#endif
};
#define PAGE_COUNT  ((int)(sizeof(k_pages) / sizeof(k_pages[0])))

static int active_index(lv_obj_t *tv)
{
    lv_obj_t *tile = lv_tileview_get_tile_active(tv);
    return tile ? (int)lv_obj_get_index(tile) : 0;
}

static void update_dots(lv_obj_t *dots, int active)
{
    for (int i = 0; i < PAGE_COUNT; i++) {
        lv_obj_t *d = lv_obj_get_child(dots, i);
        lv_obj_set_style_bg_color(d, i == active ? UI_COLOR_ACCENT : UI_COLOR_CARD_ALT, 0);
        lv_obj_set_width(d, i == active ? 24 : 10);
    }
}

static void on_tile_changed(lv_event_t *e)
{
    lv_obj_t *tv = lv_event_get_target(e);
    update_dots(lv_event_get_user_data(e), active_index(tv));
}

static void on_arrow(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target(e);
    lv_obj_t *tv = lv_event_get_user_data(e);
    int step = (int)(intptr_t)lv_obj_get_user_data(btn);
    int next = active_index(tv) + step;
    if (next >= 0 && next < PAGE_COUNT) {
        lv_tileview_set_tile_by_index(tv, next, 0, LV_ANIM_ON);
    }
}

static lv_obj_t *make_arrow(lv_obj_t *scr, lv_obj_t *tv, const char *symbol, lv_align_t align, int step)
{
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, ARROW_SIZE, ARROW_SIZE);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(btn, UI_COLOR_CARD_ALT, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_60, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_align(btn, align, align == LV_ALIGN_LEFT_MID ? 4 : -4, UI_STATUS_BAR_H / 2);
    lv_obj_set_user_data(btn, (void *)(intptr_t)step);
    lv_obj_add_event_cb(btn, on_arrow, LV_EVENT_CLICKED, tv);
    lv_obj_t *label = ui_label(btn, UI_FONT_M, UI_COLOR_TEXT, symbol);
    lv_obj_center(label);
    return btn;
}

lv_obj_t *ui_slide_view_create(void)
{
    lv_obj_t *scr = ui_screen_create();

    lv_obj_t *tv = lv_tileview_create(scr);
    lv_obj_set_size(tv, LV_PCT(100), ui_content_height());
    lv_obj_set_y(tv, UI_STATUS_BAR_H);
    lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < PAGE_COUNT; i++) {
        lv_dir_t dir = i == 0 ? LV_DIR_RIGHT : (i == PAGE_COUNT - 1 ? LV_DIR_LEFT : LV_DIR_HOR);
        lv_obj_t *tile = lv_tileview_add_tile(tv, i, 0, dir);
        // 좌우 화살표/하단 점과 겹치지 않도록 여백
        lv_obj_set_style_pad_hor(tile, ARROW_SIZE + 12, 0);
        lv_obj_set_style_pad_bottom(tile, 28, 0);
        lv_obj_set_style_pad_top(tile, 4, 0);

        k_pages[i](tile);
    }

    lv_obj_t *dots = lv_obj_create(scr);
    lv_obj_remove_style_all(dots);
    lv_obj_set_size(dots, LV_SIZE_CONTENT, 10);
    lv_obj_set_flex_flow(dots, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(dots, 8, 0);
    lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_remove_flag(dots, LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < PAGE_COUNT; i++) {
        lv_obj_t *d = lv_obj_create(dots);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, 10, 10);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    }
    update_dots(dots, 0);
    lv_obj_add_event_cb(tv, on_tile_changed, LV_EVENT_VALUE_CHANGED, dots);

    make_arrow(scr, tv, LV_SYMBOL_LEFT, LV_ALIGN_LEFT_MID, -1);
    make_arrow(scr, tv, LV_SYMBOL_RIGHT, LV_ALIGN_RIGHT_MID, +1);
    return scr;
}
