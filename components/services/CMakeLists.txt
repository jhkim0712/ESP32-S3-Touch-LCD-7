# SRCS to be added in step 5: weather.c stocks.c net_worker.c
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES app_core net json
)
