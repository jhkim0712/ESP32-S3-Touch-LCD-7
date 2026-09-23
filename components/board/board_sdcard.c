// microSD (SPI2). CS 는 CH422G EXIO4 를 상시 LOW 로 두어 선택 상태를 유지하므로
// sdspi 드라이버에는 CS 를 넘기지 않는다 (SPI 버스에 SD 카드 하나뿐).

#include "board.h"
#include "board_pins.h"

#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "board_sd";

static sdmmc_card_t *s_card;
static bool          s_bus_ready;

esp_err_t board_sdcard_mount(void)
{
    if (s_card) {
        return ESP_OK;
    }

    if (!s_bus_ready) {
        const spi_bus_config_t bus_cfg = {
            .mosi_io_num = BOARD_SD_PIN_MOSI,
            .miso_io_num = BOARD_SD_PIN_MISO,
            .sclk_io_num = BOARD_SD_PIN_SCLK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 16 * 1024,
        };
        ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_SD_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO), TAG, "spi bus");
        s_bus_ready = true;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = BOARD_SD_SPI_HOST;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.host_id = BOARD_SD_SPI_HOST;
    slot.gpio_cs = SDSPI_SLOT_NO_CS;

    const esp_vfs_fat_mount_config_t mount_cfg = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024,
    };
    esp_err_t err = esp_vfs_fat_sdspi_mount(BOARD_SD_MOUNT_POINT, &host, &slot, &mount_cfg, &s_card);
    if (err != ESP_OK) {
        s_card = NULL;
        ESP_LOGW(TAG, "SD mount failed: %s (카드 없음 또는 FAT32 아님)", esp_err_to_name(err));
        return err;
    }
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

void board_sdcard_unmount(void)
{
    if (s_card) {
        esp_vfs_fat_sdcard_unmount(BOARD_SD_MOUNT_POINT, s_card);
        s_card = NULL;
    }
}

bool board_sdcard_is_mounted(void)
{
    return s_card != NULL;
}
