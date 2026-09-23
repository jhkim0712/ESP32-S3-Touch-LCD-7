#include "ch422g.h"

#include "board_pins.h"
#include "esp_check.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define CH422G_TIMEOUT_MS   100
#define CH422G_SET_IO_OE    0x01    // WR_SET bit0: IO0~7 을 출력으로 사용

static const char *TAG = "ch422g";

static i2c_master_dev_handle_t s_dev_set;
static i2c_master_dev_handle_t s_dev_io;
static SemaphoreHandle_t       s_lock;
static uint8_t                 s_state;

static esp_err_t add_dev(i2c_master_bus_handle_t bus, uint8_t addr, i2c_master_dev_handle_t *out)
{
    const i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = BOARD_I2C_FREQ_HZ,
    };
    return i2c_master_bus_add_device(bus, &cfg, out);
}

static esp_err_t write_cmd(i2c_master_dev_handle_t dev, uint8_t value)
{
    return i2c_master_transmit(dev, &value, 1, CH422G_TIMEOUT_MS);
}

esp_err_t ch422g_init(i2c_master_bus_handle_t bus, uint8_t initial_state)
{
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");
    ESP_RETURN_ON_ERROR(add_dev(bus, CH422G_ADDR_WR_SET, &s_dev_set), TAG, "add WR_SET");
    ESP_RETURN_ON_ERROR(add_dev(bus, CH422G_ADDR_WR_IO, &s_dev_io), TAG, "add WR_IO");

    ESP_RETURN_ON_ERROR(write_cmd(s_dev_set, CH422G_SET_IO_OE), TAG, "set output mode");
    return ch422g_write(initial_state);
}

esp_err_t ch422g_write(uint8_t state)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = write_cmd(s_dev_io, state);
    if (err == ESP_OK) {
        s_state = state;
    }
    xSemaphoreGive(s_lock);
    ESP_RETURN_ON_ERROR(err, TAG, "write IO 0x%02x", state);
    return ESP_OK;
}

esp_err_t ch422g_set_bits(uint8_t mask, bool level)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    uint8_t next = level ? (s_state | mask) : (s_state & ~mask);
    esp_err_t err = write_cmd(s_dev_io, next);
    if (err == ESP_OK) {
        s_state = next;
    }
    xSemaphoreGive(s_lock);
    ESP_RETURN_ON_ERROR(err, TAG, "write IO 0x%02x", next);
    return ESP_OK;
}

uint8_t ch422g_state(void)
{
    return s_state;
}
