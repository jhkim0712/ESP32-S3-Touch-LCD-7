# SRCS to be added in step 6: github_ota.c
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES esp_https_ota app_update json app_core net
)
