idf_component_register(
    SRCS "ble_core.c" "hid_media.c" "ams_client.c"
    INCLUDE_DIRS "include"
    REQUIRES bt app_core
    PRIV_REQUIRES esp_timer
)
