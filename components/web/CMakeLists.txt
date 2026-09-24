idf_component_register(
    SRCS "web_server.c" "web_photos.c" "web_flickr.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server
    PRIV_REQUIRES app_core storage net board app_flickr esp_wifi esp_timer esp_app_format
)

# www/index.html is gzipped at build time and embedded in the app image, so an
# OTA update always brings the matching web page (LittleFS is not updated by OTA).
idf_build_get_property(python PYTHON)
set(www_src "${COMPONENT_DIR}/www/index.html")
set(www_gz "${CMAKE_CURRENT_BINARY_DIR}/index.html.gz")
add_custom_command(OUTPUT "${www_gz}"
    COMMAND "${python}" -c "import gzip,sys; open(sys.argv[2],'wb').write(gzip.compress(open(sys.argv[1],'rb').read(),9,mtime=0))" "${www_src}" "${www_gz}"
    DEPENDS "${www_src}"
    VERBATIM)
target_add_binary_data(${COMPONENT_LIB} "${www_gz}" BINARY)
