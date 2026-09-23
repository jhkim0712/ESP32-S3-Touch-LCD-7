// OTA 업데이트 팝업: 새 버전 알림 → [나중에] / [업데이트] → 진행률 → 결과
// 실제 다운로드/설치는 ota 컴포넌트(6단계)가 APP_EVT_REQ_OTA_START 를 받아 수행한다.

#include "ui_internal.h"

#include "app_state.h"
#include "esp_app_desc.h"

static lv_obj_t *s_mbox;
static lv_obj_t *s_bar;
static lv_obj_t *s_status;

static void on_deleted(lv_event_t *e)
{
    s_mbox = NULL;
    s_bar = NULL;
    s_status = NULL;
}

static void on_close(lv_event_t *e)
{
    if (s_mbox) {
        lv_msgbox_close(s_mbox);
    }
}

static void on_update(lv_event_t *e)
{
    lv_obj_t *footer = lv_msgbox_get_footer(s_mbox);
    if (footer) {
        lv_obj_add_flag(footer, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t *content = lv_msgbox_get_content(s_mbox);
    s_bar = lv_bar_create(content);
    lv_obj_set_size(s_bar, LV_PCT(100), 14);
    lv_bar_set_range(s_bar, 0, 100);
    s_status = ui_label(content, UI_FONT_S, UI_COLOR_DIM, "Downloading...");
    app_event_post(APP_EVT_REQ_OTA_START, NULL, 0);
}

void ui_ota_popup_show(const ota_release_t *rel)
{
    if (s_mbox) {
        return;
    }
    s_mbox = lv_msgbox_create(NULL);   // lv_layer_top 위 모달
    lv_obj_set_width(s_mbox, ui_is_portrait() ? LV_PCT(90) : 560);
    lv_obj_add_event_cb(s_mbox, on_deleted, LV_EVENT_DELETE, NULL);

    lv_msgbox_add_title(s_mbox, LV_SYMBOL_DOWNLOAD "  Firmware update available");
    lv_msgbox_add_text_fmt(s_mbox, "v%s  " LV_SYMBOL_RIGHT "  %s\n\n%s",
                           esp_app_get_description()->version, rel->tag, rel->notes);
    lv_obj_add_event_cb(lv_msgbox_add_footer_button(s_mbox, "Later"), on_close, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(lv_msgbox_add_footer_button(s_mbox, "Update"), on_update, LV_EVENT_CLICKED, NULL);
}

void ui_ota_popup_progress(int percent)
{
    if (s_bar) {
        lv_bar_set_value(s_bar, percent, LV_ANIM_ON);
        lv_label_set_text_fmt(s_status, "Downloading... %d%%", percent);
    }
}

void ui_ota_popup_done(esp_err_t result)
{
    if (!s_mbox || !s_status) {
        return;
    }
    if (result == ESP_OK) {
        lv_label_set_text(s_status, LV_SYMBOL_OK "  Update installed. Restarting...");
        return;
    }
    lv_label_set_text_fmt(s_status, LV_SYMBOL_WARNING "  Update failed: %s", esp_err_to_name(result));
    lv_obj_t *footer = lv_msgbox_get_footer(s_mbox);
    if (footer) {
        lv_obj_clean(footer);
        lv_obj_remove_flag(footer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(lv_msgbox_add_footer_button(s_mbox, "Close"), on_close, LV_EVENT_CLICKED, NULL);
    }
}
