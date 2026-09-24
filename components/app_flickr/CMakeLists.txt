idf_component_register(
    SRCS "app_flickr.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES
        esp_http_client
        mbedtls
        app_core
        storage
        net
        board
)
