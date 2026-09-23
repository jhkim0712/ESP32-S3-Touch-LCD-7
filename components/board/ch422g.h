// CH422G I2C IO 확장기 (board 컴포넌트 내부용).
// 레지스터가 없고 I2C 주소 자체가 명령이라, 명령마다 별도 I2C 디바이스로 등록한다.
// 출력 상태를 캐시해 두고 비트 단위로 변경한다 (스레드 안전).
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

esp_err_t ch422g_init(i2c_master_bus_handle_t bus, uint8_t initial_state);
esp_err_t ch422g_write(uint8_t state);
esp_err_t ch422g_set_bits(uint8_t mask, bool level);
uint8_t   ch422g_state(void);
