// 위젯형 화면: 한 화면에 카드 4개
//   가로(800x480): 2x2   [시계 | 날씨]
//                        [주가 | 음악]
//   세로(480x800): 1x4   시계 / 날씨 / 주가 / 음악

#include "ui_internal.h"

static const int32_t s_cols_land[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
static const int32_t s_rows_land[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
static const int32_t s_cols_port[] = { LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
static const int32_t s_rows_port[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                       LV_GRID_TEMPLATE_LAST };

typedef void (*card_fn_t)(lv_obj_t *parent, bool full);

lv_obj_t *ui_widget_view_create(void)
{
    lv_obj_t *scr = ui_screen_create();
    bool portrait = ui_is_portrait();

    lv_obj_t *grid = lv_obj_create(scr);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), ui_content_height());
    lv_obj_set_y(grid, UI_STATUS_BAR_H);
    lv_obj_set_style_pad_all(grid, UI_GAP, 0);
    lv_obj_set_style_pad_top(grid, 0, 0);
    lv_obj_set_style_pad_row(grid, UI_GAP, 0);
    lv_obj_set_style_pad_column(grid, UI_GAP, 0);
    lv_obj_set_grid_dsc_array(grid, portrait ? s_cols_port : s_cols_land,
                              portrait ? s_rows_port : s_rows_land);

    static const card_fn_t cards[] = { ui_clock_create, ui_weather_create, ui_stocks_create, ui_music_create };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *card = ui_card_create(grid);
        int col = portrait ? 0 : i % 2;
        int row = portrait ? i : i / 2;
        lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, col, 1, LV_GRID_ALIGN_STRETCH, row, 1);
        cards[i](card, false);
    }
    return scr;
}
