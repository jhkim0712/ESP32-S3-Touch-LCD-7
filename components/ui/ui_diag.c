// 3단계 하드웨어 진단 화면
//   - 상단 색상 막대: R/G/B 데이터 핀 순서와 비트 누락 확인 (그라데이션이 매끄러워야 함)
//   - 터치 좌표: 네 모서리를 눌러 0..799 / 0..479 범위와 방향 확인
//   - 버튼 카운터: 탭 인식 확인
//   - 가동 시간 + 움직이는 막대: 화면 갱신과 찢어짐(tearing) 확인

#include "ui_internal.h"

#include <stdio.h>
#include "board_pins.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

typedef struct {
    lv_obj_t *touch_label;
    lv_obj_t *uptime_label;
    lv_obj_t *heap_label;
    lv_obj_t *mover;
    lv_obj_t *dot;
    int       clicks;
} diag_ctx_t;

static diag_ctx_t s_ctx;

static lv_obj_t *make_bar(lv_obj_t *parent, int y, lv_color_t from, lv_color_t to, const char *name)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, BOARD_LCD_H_RES - 120, 32);
    lv_obj_set_pos(bar, 110, y);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, from, 0);
    lv_obj_set_style_bg_grad_color(bar, to, 0);
    lv_obj_set_style_bg_grad_dir(bar, LV_GRAD_DIR_HOR, 0);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, name);
    lv_obj_set_pos(label, 10, y + 6);
    return bar;
}

static void on_screen_touch(lv_event_t *e)
{
    lv_indev_t *indev = lv_indev_active();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_label_set_text_fmt(s_ctx.touch_label, "Touch: x=%d  y=%d", (int)p.x, (int)p.y);
    lv_obj_set_pos(s_ctx.dot, p.x - 10, p.y - 10);
    lv_obj_remove_flag(s_ctx.dot, LV_OBJ_FLAG_HIDDEN);
}

static void on_button(lv_event_t *e)
{
    lv_obj_t *label = lv_event_get_user_data(e);
    lv_label_set_text_fmt(label, "Tapped %d", ++s_ctx.clicks);
}

static void on_tick(lv_timer_t *t)
{
    int64_t s = esp_timer_get_time() / 1000000;
    lv_label_set_text_fmt(s_ctx.uptime_label, "Uptime %02d:%02d:%02d",
                          (int)(s / 3600), (int)(s / 60 % 60), (int)(s % 60));
    lv_label_set_text_fmt(s_ctx.heap_label, "Heap  internal %u KB  |  PSRAM %u KB",
                          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

static void mover_anim(void *obj, int32_t x)
{
    lv_obj_set_x(obj, x);
}

void ui_diag_create(lv_obj_t *parent)
{
    lv_obj_set_style_bg_color(parent, lv_color_hex(0x101418), 0);
    lv_obj_set_style_text_color(parent, lv_color_white(), 0);
    lv_obj_set_style_text_font(parent, &lv_font_montserrat_20, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(parent, on_screen_touch, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(parent, on_screen_touch, LV_EVENT_PRESSED, NULL);

    lv_obj_t *title = lv_label_create(parent);
    lv_label_set_text_fmt(title, "ESP32-S3-Touch-LCD-7  |  v%s  |  Step 3 diagnostics",
                          esp_app_get_description()->version);
    lv_obj_set_pos(title, 10, 8);

    make_bar(parent, 44, lv_color_black(), lv_color_hex(0xFF0000), "RED");
    make_bar(parent, 82, lv_color_black(), lv_color_hex(0x00FF00), "GREEN");
    make_bar(parent, 120, lv_color_black(), lv_color_hex(0x0000FF), "BLUE");
    make_bar(parent, 158, lv_color_black(), lv_color_white(), "WHITE");

    s_ctx.touch_label = lv_label_create(parent);
    lv_label_set_text(s_ctx.touch_label, "Touch: tap anywhere / corners");
    lv_obj_set_pos(s_ctx.touch_label, 10, 210);

    s_ctx.uptime_label = lv_label_create(parent);
    lv_obj_set_pos(s_ctx.uptime_label, 10, 245);

    s_ctx.heap_label = lv_label_create(parent);
    lv_obj_set_style_text_font(s_ctx.heap_label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(s_ctx.heap_label, 10, 280);

    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 200, 70);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -20, -70);
    lv_obj_t *btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Tap me");
    lv_obj_center(btn_label);
    lv_obj_add_event_cb(btn, on_button, LV_EVENT_CLICKED, btn_label);

    // 좌우로 움직이는 막대: 찢어짐(tearing)이나 잔상이 보이면 안 된다
    s_ctx.mover = lv_obj_create(parent);
    lv_obj_remove_style_all(s_ctx.mover);
    lv_obj_set_size(s_ctx.mover, 40, 40);
    lv_obj_set_y(s_ctx.mover, BOARD_LCD_V_RES - 50);
    lv_obj_set_style_bg_opa(s_ctx.mover, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_ctx.mover, lv_color_hex(0xFFB000), 0);
    lv_obj_remove_flag(s_ctx.mover, LV_OBJ_FLAG_CLICKABLE);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_ctx.mover);
    lv_anim_set_exec_cb(&a, mover_anim);
    lv_anim_set_values(&a, 0, BOARD_LCD_H_RES - 40);
    lv_anim_set_duration(&a, 2000);
    lv_anim_set_playback_duration(&a, 2000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    // 터치 위치 표시 점
    s_ctx.dot = lv_obj_create(parent);
    lv_obj_remove_style_all(s_ctx.dot);
    lv_obj_set_size(s_ctx.dot, 20, 20);
    lv_obj_set_style_radius(s_ctx.dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_ctx.dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_ctx.dot, lv_color_hex(0x00E0FF), 0);
    lv_obj_add_flag(s_ctx.dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_ctx.dot, LV_OBJ_FLAG_CLICKABLE);

    on_tick(NULL);
    lv_timer_create(on_tick, 1000, NULL);
}
