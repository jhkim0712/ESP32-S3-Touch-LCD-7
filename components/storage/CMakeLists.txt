# SRCS to be added in step 5: app_storage.c settings.c
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash littlefs
)
