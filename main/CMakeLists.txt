idf_component_register(
    SRCS "main.cpp" "demo_data.c"
    INCLUDE_DIRS "."
    REQUIRES board storage app_core net services media_ble photo ota ui nvs_flash esp_app_format
)
