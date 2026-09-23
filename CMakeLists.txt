# ESP32-S3-Touch-LCD-7 Smart Display
# ESP-IDF v5.3+ (recommended v5.4+)
#
# Firmware version comes from version.txt -> esp_app_desc_t.version,
# used by the GitHub Release OTA check. CI overrides it from the tag.
# NOTE: keep IDF build/config files ASCII-only (Windows cp949 locale).
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(smart_display)

# LittleFS image of assets/ (fonts, icons) is built and flashed together with
# the app by `idf.py flash`. It overwrites the "storage" partition.
littlefs_create_partition_image(storage assets FLASH_IN_PROJECT)
