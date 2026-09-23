idf_component_register(
    SRCS "ui_core.c" "ui_theme.c" "status_bar.c" "widget_view.c" "slide_view.c" "popup_ota.c"
         "pages/page_clock.c" "pages/page_weather.c" "pages/page_stocks.c"
         "pages/page_music.c" "pages/page_photo.c" "pages/page_settings.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES board app_core storage media_ble photo ota lvgl esp_lvgl_port
    PRIV_REQUIRES esp_app_format
)
