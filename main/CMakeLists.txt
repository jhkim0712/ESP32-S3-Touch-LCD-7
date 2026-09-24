idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES board storage app_core net services media_ble photo ota ui web photo_feed nvs_flash esp_app_format
)
