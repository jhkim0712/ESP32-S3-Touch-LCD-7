// 위젯형 화면: 한 화면에 카드 배치
//   가로(800x480): 2x2   [시계 | 날씨]          BLE 음악 리모컨이 꺼져 있으면  [시계 | 날씨]
//                        [주가 | 음악]                                          [   주가    ]
//   세로(480x800): 한 열  시계 / 날씨 / 주가 (/ 음악)

#include "ui_internal.h"

#include "sdkconfig.h"

typedef void (*card_fn_t)(lv_obj_t *parent, bool full);

static const card_fn_t k_cards[] = {
    ui_clock_create,
    ui_weather_create,
    ui_stocks_create,
#if CONFIG_APP_BLE_MEDIA
    ui_music_create,
#endif
};
#define CARD_COUNT  ((int)(sizeof(k_cards) / sizeof(k_cards[0])))

static const int32_t s_cols_land[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
static const int32_t s_rows_land[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
static const int32_t s_cols_port[] = { LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST };
static const int32_t s_rows_port[] = { LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_FR(1),
                                       LV_GRID_TEMPLATE_LAST };
static int32_t       s_rows_port_n[CARD_COUNT + 1];

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

    const int32_t *rows = s_rows_port;
    if (portrait && CARD_COUNT != 4) {
        for (int i = 0; i < CARD_COUNT; i++) {
            s_rows_port_n[i] = LV_GRID_FR(1);
        }
        s_rows_port_n[CARD_COUNT] = LV_GRID_TEMPLATE_LAST;
        rows = s_rows_port_n;
    }
    lv_obj_set_grid_dsc_array(grid, portrait ? s_cols_port : s_cols_land, portrait ? rows : s_rows_land);

    for (int i = 0; i < CARD_COUNT; i++) {
        lv_obj_t *card = ui_card_create(grid);
        int col = portrait ? 0 : i % 2;
        int row = portrait ? i : i / 2;
        // 가로 배치에서 마지막 카드가 혼자 남으면 두 칸을 차지한다
        int span = (!portrait && i == CARD_COUNT - 1 && CARD_COUNT % 2) ? 2 : 1;
        lv_obj_set_grid_cell(card, LV_GRID_ALIGN_STRETCH, col, span, LV_GRID_ALIGN_STRETCH, row, 1);
        k_cards[i](card, false);
    }
    return scr;
}
