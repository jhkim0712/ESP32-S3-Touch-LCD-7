# SRCS to be added in step 5: photo_loader.c
idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES app_core board esp_jpeg
)
