// OTA 업데이트 팝업: 새 버전 알림 → [나중에] / [업데이트] → 진행률 → 결과
// 실제 다운로드/설치는 net_worker 가 APP_EVT_REQ_OTA_START 를 받아 ota_install() 로 수행한다.
// 웹 설정 페이지에서 설치를 시작하면 첫 진행률 이벤트에서 진행 상황 창을 연다.

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

// 버튼을 숨기고 진행 막대를 붙인다
static void show_progress(void)
{
    lv_obj_t *footer = lv_msgbox_get_footer(s_mbox);
    if (footer) {
        lv_obj_add_flag(footer, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t *content = lv_msgbox_get_content(s_mbox);
    s_bar = lv_bar_create(content);
    lv_obj_set_size(s_bar, LV_PCT(100), 14);
    lv_bar_set_range(s_bar, 0, 100);
    s_status = ui_label(content, UI_FONT_S, UI_COLOR_DIM, TR("Downloading...", "다운로드 중..."));
}

static void on_update(lv_event_t *e)
{
    show_progress();
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

    lv_msgbox_add_title(s_mbox, TR(LV_SYMBOL_DOWNLOAD "  Firmware update available",
                                   LV_SYMBOL_DOWNLOAD "  새 펌웨어가 있습니다"));
    lv_msgbox_add_text_fmt(s_mbox, "v%s  " LV_SYMBOL_RIGHT "  %s\n\n%s",
                           esp_app_get_description()->version, rel->tag, rel->notes);
    lv_obj_add_event_cb(lv_msgbox_add_footer_button(s_mbox, TR("Later", "나중에")), on_close, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(lv_msgbox_add_footer_button(s_mbox, TR("Update", "업데이트")), on_update, LV_EVENT_CLICKED, NULL);
}

void ui_ota_popup_progress(int percent)
{
    if (!s_mbox) {
        // 웹 설정 페이지에서 설치를 시작한 경우: 진행 상황만 보여 주는 창
        s_mbox = lv_msgbox_create(NULL);
        lv_obj_set_width(s_mbox, ui_is_portrait() ? LV_PCT(90) : 560);
        lv_obj_add_event_cb(s_mbox, on_deleted, LV_EVENT_DELETE, NULL);
        lv_msgbox_add_title(s_mbox, TR(LV_SYMBOL_DOWNLOAD "  Updating firmware", LV_SYMBOL_DOWNLOAD "  펌웨어 업데이트 중"));
    }
    if (!s_bar) {
        show_progress();
    }
    if (s_bar) {
        lv_bar_set_value(s_bar, percent, LV_ANIM_ON);
        lv_label_set_text_fmt(s_status, TR("Downloading... %d%%", "다운로드 중... %d%%"), percent);
    }
}

void ui_ota_popup_done(esp_err_t result)
{
    if (!s_mbox || !s_status) {
        return;
    }
    if (result == ESP_OK) {
        lv_label_set_text(s_status, TR(LV_SYMBOL_OK "  Update installed. Restarting...",
                                       LV_SYMBOL_OK "  설치 완료. 다시 시작합니다..."));
        return;
    }
    lv_label_set_text_fmt(s_status, TR(LV_SYMBOL_WARNING "  Update failed: %s", LV_SYMBOL_WARNING "  업데이트 실패: %s"),
                          esp_err_to_name(result));
    lv_obj_t *footer = lv_msgbox_get_footer(s_mbox);
    if (footer) {
        lv_obj_clean(footer);
        lv_obj_remove_flag(footer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(lv_msgbox_add_footer_button(s_mbox, TR("Close", "닫기")), on_close, LV_EVENT_CLICKED, NULL);
    }
}
