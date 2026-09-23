idf_component_register(
    SRCS "ui_core.c" "ui_theme.c" "lv_mem_psram.c" "status_bar.c" "widget_view.c" "slide_view.c" "popup_ota.c"
         "pages/page_clock.c" "pages/page_weather.c" "pages/page_stocks.c"
         "pages/page_music.c" "pages/page_photo.c" "pages/page_settings.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES board app_core storage media_ble photo ota lvgl esp_lvgl_port
    PRIV_REQUIRES esp_app_format
)

# LVGL (linked earlier) calls lv_malloc_core() etc. from lv_mem_psram.c; make the
# linker pull that object out of this archive even though nothing in ui uses it.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u lv_malloc_core")
