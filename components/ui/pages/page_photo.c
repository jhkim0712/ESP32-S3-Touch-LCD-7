// 전자앨범 페이지: photo 컴포넌트가 PSRAM 에 미리 디코딩한 RGB565 프레임을 표시만 한다.
// 화면 왼쪽/오른쪽 절반을 탭하면 이전/다음 사진을 요청한다.

#include "ui_internal.h"

#include <string.h>
#include "app_state.h"
#include "photo.h"

typedef struct {
    lv_obj_t       *image;
    lv_obj_t       *placeholder;
    lv_obj_t       *caption;
    lv_image_dsc_t  dsc[2];     // 같은 디스크립터를 재사용하면 LVGL 이 변경을 놓칠 수 있어 번갈아 사용
    int             next;
} photo_view_t;

static void photo_cb(lv_observer_t *o, lv_subject_t *s)
{
    lv_obj_t *root = lv_observer_get_target_obj(o);
    photo_view_t *v = lv_obj_get_user_data(root);
    const photo_frame_t *f = lv_subject_get_pointer(s);

    if (!f || !f->pixels) {
        lv_obj_add_flag(v->image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(v->placeholder, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(v->caption, "");
        return;
    }

    lv_image_dsc_t *d = &v->dsc[v->next];
    v->next ^= 1;
    // 디스크립터 주소를 재사용하지만 LVGL 이미지 캐시가 꺼져 있어(LV_CACHE_DEF_SIZE=0)
    // 이전 사진이 남지 않는다. 캐시를 켜면 여기서 lv_image_cache_drop(d) 가 필요하다.
    lv_memzero(d, sizeof(*d));
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565;
    d->header.w = f->width;
    d->header.h = f->height;
    d->header.stride = f->width * 2;
    d->data_size = (uint32_t)f->width * f->height * 2;
    d->data = (const uint8_t *)f->pixels;

    lv_image_set_src(v->image, d);
    lv_obj_center(v->image);
    lv_obj_remove_flag(v->image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(v->placeholder, LV_OBJ_FLAG_HIDDEN);

    const char *name = strrchr(f->path, '/');
    lv_label_set_text(v->caption, name ? name + 1 : f->path);
}

static void on_tap(lv_event_t *e)
{
    lv_obj_t *root = lv_event_get_current_target(e);
    lv_point_t p;
    lv_indev_get_point(lv_indev_active(), &p);
    lv_area_t a;
    lv_obj_get_coords(root, &a);
    bool left = p.x < a.x1 + lv_area_get_width(&a) / 2;
    app_event_post(left ? APP_EVT_REQ_PHOTO_PREV : APP_EVT_REQ_PHOTO_NEXT, NULL, 0);
}

void ui_photo_create(lv_obj_t *parent)
{
    photo_view_t *v = lv_malloc_zeroed(sizeof(photo_view_t));
    lv_obj_set_user_data(parent, v);
    ui_free_user_data_on_delete(parent);

    lv_obj_set_style_bg_color(parent, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_add_flag(parent, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(parent, on_tap, LV_EVENT_SHORT_CLICKED, NULL);

    v->image = lv_image_create(parent);
    lv_obj_add_flag(v->image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(v->image, LV_OBJ_FLAG_CLICKABLE);

    v->placeholder = ui_label(parent, UI_FONT_M, UI_COLOR_DIM,
                              LV_SYMBOL_IMAGE "  No photos\n\nPut JPG/PNG files in /photos on the SD card");
    lv_obj_set_style_text_align(v->placeholder, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(v->placeholder);

    v->caption = ui_label(parent, UI_FONT_S, UI_COLOR_DIM, "");
    lv_obj_align(v->caption, LV_ALIGN_BOTTOM_MID, 0, -30);

    lv_subject_add_observer_obj(&ui_subj_photo, photo_cb, parent, NULL);
}
