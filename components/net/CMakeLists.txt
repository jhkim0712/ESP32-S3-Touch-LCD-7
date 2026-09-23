# SRCS to be added in step 5: wifi_mgr.c time_sync.c http_util.c
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_http_client mbedtls app_core
)
