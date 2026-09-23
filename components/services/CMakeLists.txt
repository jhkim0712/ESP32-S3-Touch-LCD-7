idf_component_register(
    SRCS "weather.c" "stocks.c" "net_worker.c"
    INCLUDE_DIRS "include"
    REQUIRES app_core net cjson
    PRIV_REQUIRES storage esp_timer
)
