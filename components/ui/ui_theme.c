// 공통 테마, 카드 스타일, 포맷 헬퍼

#include "ui_internal.h"

#include <math.h>
#include <string.h>

#define FONT_KR_PATH        "S:" STORAGE_LFS_MOUNT_POINT "/fonts/NotoSansKR-subset.ttf"   // S: = LVGL stdio 드라이버
#define FONT_WEATHER_PATH   "S:" STORAGE_LFS_MOUNT_POINT "/fonts/weather-icons.ttf"

static lv_style_t s_card;
static lv_style_t s_screen;

const lv_font_t *ui_font_s = &lv_font_montserrat_14;
const lv_font_t *ui_font_m = &lv_font_montserrat_20;
const lv_font_t *ui_font_l = &lv_font_montserrat_28;
const lv_font_t *ui_font_weather_l;
const lv_font_t *ui_font_weather_s;

static bool       s_korean_font;
static app_lang_t s_lang = APP_LANG_EN;

static const lv_font_t *load_ttf(const char *path, int32_t size, size_t cache, const lv_font_t *fallback)
{
    lv_font_t *f = lv_tiny_ttf_create_file_ex(path, size, LV_FONT_KERNING_NONE, cache);
    if (f) {
        f->fallback = fallback;
    }
    return f;
}

void ui_fonts_init(void)
{
    const lv_font_t *s = load_ttf(FONT_KR_PATH, 15, 256, &lv_font_montserrat_14);
    const lv_font_t *m = s ? load_ttf(FONT_KR_PATH, 20, 384, &lv_font_montserrat_20) : NULL;
    const lv_font_t *l = m ? load_ttf(FONT_KR_PATH, 28, 128, &lv_font_montserrat_28) : NULL;
    if (s && m && l) {
        ui_font_s = s;
        ui_font_m = m;
        ui_font_l = l;
        s_korean_font = true;
    } else {
        LV_LOG_WARN("Korean font not found (%s) - UI stays in English", FONT_KR_PATH);
    }
    ui_font_weather_l = load_ttf(FONT_WEATHER_PATH, 88, 16, NULL);
    ui_font_weather_s = load_ttf(FONT_WEATHER_PATH, 52, 16, NULL);
}

void ui_lang_set(app_lang_t lang)
{
    s_lang = lang;
}

app_lang_t ui_lang_setting(void)
{
    return s_lang;
}

app_lang_t ui_lang(void)
{
    return s_korean_font ? s_lang : APP_LANG_EN;
}

void ui_fmt_date(char *buf, size_t size, const struct tm *tm, bool long_form)
{
    if (ui_lang() == APP_LANG_KO) {
        static const char *const wday[] = { "일", "월", "화", "수", "목", "금", "토" };
        if (long_form) {
            lv_snprintf(buf, size, "%d년 %d월 %d일 %s요일", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                        wday[tm->tm_wday]);
        } else {
            lv_snprintf(buf, size, "%d월 %d일 (%s)", tm->tm_mon + 1, tm->tm_mday, wday[tm->tm_wday]);
        }
        return;
    }
    strftime(buf, size, long_form ? "%A, %d %B %Y" : "%a %d %b", tm);
}

void ui_theme_init(lv_display_t *disp)
{
    lv_theme_t *th = lv_theme_default_init(disp, UI_COLOR_ACCENT, UI_COLOR_WARM, true, UI_FONT_M);
    lv_display_set_theme(disp, th);

    lv_style_init(&s_screen);
    lv_style_set_bg_color(&s_screen, UI_COLOR_BG);
    lv_style_set_bg_opa(&s_screen, LV_OPA_COVER);
    lv_style_set_text_color(&s_screen, UI_COLOR_TEXT);
    lv_style_set_text_font(&s_screen, UI_FONT_M);

    lv_style_init(&s_card);
    lv_style_set_bg_color(&s_card, UI_COLOR_CARD);
    lv_style_set_bg_opa(&s_card, LV_OPA_COVER);
    lv_style_set_radius(&s_card, 16);
    lv_style_set_border_width(&s_card, 0);
    lv_style_set_pad_all(&s_card, 16);
    lv_style_set_pad_row(&s_card, 6);
    lv_style_set_pad_column(&s_card, 12);
}

lv_obj_t *ui_screen_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_add_style(scr, &s_screen, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

lv_obj_t *ui_card_create(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_add_style(card, &s_card, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    return card;
}

lv_obj_t *ui_card_title(lv_obj_t *card, const char *symbol, const char *text)
{
    lv_obj_t *label = ui_label(card, UI_FONT_S, UI_COLOR_DIM, "");
    lv_label_set_text_fmt(label, "%s  %s", symbol ? symbol : "", text);
    return label;
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, text);
    return label;
}

bool ui_is_portrait(void)
{
    lv_display_t *d = lv_display_get_default();
    return lv_display_get_vertical_resolution(d) > lv_display_get_horizontal_resolution(d);
}

int32_t ui_content_height(void)
{
    return lv_display_get_vertical_resolution(lv_display_get_default()) - UI_STATUS_BAR_H;
}

// printf 의 부동소수점/long long 지원 여부(libc 설정)에 의존하지 않도록 직접 변환한다.
// 예) 2615.4 -> "2,615.40" (decimals=2), 71500 -> "71,500" (decimals=0)
void ui_fmt_number(char *buf, size_t size, double value, int decimals)
{
    char tmp[40];
    int  pos = sizeof(tmp) - 1;
    tmp[pos] = '\0';

    bool neg = value < 0;
    int64_t scale = 1;
    for (int i = 0; i < decimals; i++) {
        scale *= 10;
    }
    int64_t fixed = (int64_t)llround(fabs(value) * (double)scale);
    int64_t ipart = fixed / scale;
    int64_t fpart = fixed % scale;

    for (int i = 0; i < decimals; i++) {
        tmp[--pos] = (char)('0' + fpart % 10);
        fpart /= 10;
    }
    if (decimals > 0) {
        tmp[--pos] = '.';
    }
    int digits = 0;
    do {
        if (digits > 0 && digits % 3 == 0) {
            tmp[--pos] = ',';
        }
        tmp[--pos] = (char)('0' + ipart % 10);
        ipart /= 10;
        digits++;
    } while (ipart > 0 && pos > 1);
    if (neg && fixed != 0) {
        tmp[--pos] = '-';
    }
    strlcpy(buf, &tmp[pos], size);
}

static void free_user_data_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    lv_free(lv_obj_get_user_data(obj));
    lv_obj_set_user_data(obj, NULL);
}

void ui_free_user_data_on_delete(lv_obj_t *obj)
{
    lv_obj_add_event_cb(obj, free_user_data_cb, LV_EVENT_DELETE, NULL);
}
