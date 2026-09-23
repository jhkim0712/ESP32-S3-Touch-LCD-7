# SRCS to be added in step 5: ble_core.c ams_client.c hid_media.c
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES bt app_core
)
