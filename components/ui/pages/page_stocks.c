// 주가: 종목별 현재가와 등락률 (상승 빨강 / 하락 파랑)
//   카드: 최대 4종목, 슬라이드: 전체 (APP_MAX_STOCKS)

#include "ui_internal.h"

#include <math.h>
#include <string.h>
#include <time.h>
#include "app_state.h"

#define COMPACT_ROWS 4

typedef struct {
    lv_obj_t *list;
    lv_obj_t *updated;
    bool      full;
} stocks_view_t;

static void add_row(lv_obj_t *list, const stock_quote_t *q, bool full)
{
    lv_obj_t *row = lv_obj_create(list);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 10, 0);
    lv_obj_set_style_pad_ver(row, full ? 6 : 2, 0);

    lv_obj_t *name = ui_label(row, UI_FONT_M, UI_COLOR_TEXT, q->name[0] ? q->name : q->symbol);
    lv_obj_set_flex_grow(name, 1);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);

    // 원화는 소수점 없이, 그 외는 소수 2자리
    char price[24];
    ui_fmt_number(price, sizeof(price), q->price, strcmp(q->currency, "KRW") == 0 ? 0 : 2);
    ui_label(row, UI_FONT_M, UI_COLOR_TEXT, price);

    bool up = q->change_pct >= 0;
    char pct[16];
    ui_fmt_number(pct, sizeof(pct), fabs(q->change_pct), 2);
    lv_obj_t *badge = ui_label(row, UI_FONT_S, UI_COLOR_BG, "");
    lv_label_set_text_fmt(badge, "%s%s%%", up ? "+" : "-", pct);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(badge, up ? UI_COLOR_UP : UI_COLOR_DOWN, 0);
    lv_obj_set_style_radius(badge, 6, 0);
    lv_obj_set_style_pad_hor(badge, 8, 0);
    lv_obj_set_style_pad_ver(badge, 3, 0);
    lv_obj_set_width(badge, 84);
    lv_obj_set_style_text_align(badge, LV_TEXT_ALIGN_CENTER, 0);
}

static void stocks_cb(lv_observer_t *o, lv_subject_t *s)
{
    lv_obj_t *root = lv_observer_get_target_obj(o);
    stocks_view_t *v = lv_obj_get_user_data(root);

    stock_list_t *list = lv_malloc(sizeof(stock_list_t));   // 약 600B, LVGL 태스크 스택 절약
    if (!list) {
        return;
    }
    app_state_get_stocks(list);

    lv_obj_clean(v->list);
    int limit = v->full ? APP_MAX_STOCKS : COMPACT_ROWS;
    for (int i = 0; i < list->count && i < limit; i++) {
        add_row(v->list, &list->items[i], v->full);
    }
    if (list->count == 0) {
        ui_label(v->list, UI_FONT_M, UI_COLOR_DIM, "No data yet");
    }

    if (v->updated) {
        if (list->updated_at > 0) {
            struct tm tm;
            localtime_r(&list->updated_at, &tm);
            char buf[32];
            strftime(buf, sizeof(buf), "Updated %H:%M:%S", &tm);
            lv_label_set_text(v->updated, buf);
        } else {
            lv_label_set_text(v->updated, "");
        }
    }
    lv_free(list);
}

void ui_stocks_create(lv_obj_t *parent, bool full)
{
    stocks_view_t *v = lv_malloc_zeroed(sizeof(stocks_view_t));
    v->full = full;
    lv_obj_set_user_data(parent, v);
    ui_free_user_data_on_delete(parent);

    ui_card_title(parent, LV_SYMBOL_SHUFFLE, "Stocks");

    v->list = lv_obj_create(parent);
    lv_obj_remove_style_all(v->list);
    lv_obj_set_width(v->list, LV_PCT(100));
    lv_obj_set_flex_grow(v->list, 1);
    lv_obj_set_flex_flow(v->list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_top(v->list, 6, 0);

    if (full) {
        v->updated = ui_label(parent, UI_FONT_S, UI_COLOR_DIM, "");
    }
    lv_subject_add_observer_obj(&ui_subj_stocks, stocks_cb, parent, NULL);
}
