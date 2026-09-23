# http_util.c (HTTPS GET into PSRAM) is added with the weather/stock services (step 5-3)
idf_component_register(
    SRCS "wifi_mgr.c" "time_sync.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event esp_http_client mbedtls app_core storage
    PRIV_REQUIRES esp_timer lwip
)
