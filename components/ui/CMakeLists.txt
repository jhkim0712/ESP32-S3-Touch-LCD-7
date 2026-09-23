# SRCS to be added in step 3~4:
#   ui_core.cpp ui_theme.cpp widget_view.cpp slide_view.cpp status_bar.cpp
#   pages/page_clock.cpp pages/page_photo.cpp pages/page_weather_stock.cpp
#   pages/page_music.cpp pages/popup_ota.cpp pages/page_settings.cpp
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES board app_core storage services media_ble photo ota lvgl esp_lvgl_port
)
