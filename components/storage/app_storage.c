#include "app_storage.h"

#include "esp_check.h"
#include "esp_littlefs.h"
#include "esp_log.h"

static const char *TAG = "storage";

esp_err_t storage_init(void)
{
    const esp_vfs_littlefs_conf_t conf = {
        .base_path = STORAGE_LFS_MOUNT_POINT,
        .partition_label = "storage",
        .format_if_mount_failed = true,
    };
    ESP_RETURN_ON_ERROR(esp_vfs_littlefs_register(&conf), TAG, "littlefs mount");

    size_t total = 0, used = 0;
    if (esp_littlefs_info(conf.partition_label, &total, &used) == ESP_OK) {
        ESP_LOGI(TAG, "LittleFS %s: %u / %u KB used", STORAGE_LFS_MOUNT_POINT,
                 (unsigned)(used / 1024), (unsigned)(total / 1024));
    }
    return ESP_OK;
}
