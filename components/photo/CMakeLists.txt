idf_component_register(
    SRCS "photo_loader.c"
    INCLUDE_DIRS "include"
    REQUIRES app_core
    PRIV_REQUIRES board storage esp_rom esp_timer lvgl
)
