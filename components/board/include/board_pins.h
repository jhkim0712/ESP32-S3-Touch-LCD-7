// Waveshare ESP32-S3-Touch-LCD-7 하드웨어 매핑
// 출처: Waveshare 회로도 / 공식 ESP-IDF·ESP32_Display_Panel 예제
#pragma once

#include "driver/gpio.h"

// ---------------------------------------------------------------------------
// RGB LCD (ST7262, 800x480, RGB565 16-bit parallel)
// ---------------------------------------------------------------------------
#define BOARD_LCD_H_RES             800
#define BOARD_LCD_V_RES             480
#define BOARD_LCD_PCLK_HZ           (16 * 1000 * 1000)   // bounce buffer 사용 시 18~21MHz 까지 시도 가능

#define BOARD_LCD_HSYNC_PULSE       4
#define BOARD_LCD_HSYNC_BACK_PORCH  8
#define BOARD_LCD_HSYNC_FRONT_PORCH 8
#define BOARD_LCD_VSYNC_PULSE       4
#define BOARD_LCD_VSYNC_BACK_PORCH  8
#define BOARD_LCD_VSYNC_FRONT_PORCH 8
#define BOARD_LCD_PCLK_ACTIVE_NEG   1

#define BOARD_LCD_PIN_HSYNC         GPIO_NUM_46
#define BOARD_LCD_PIN_VSYNC         GPIO_NUM_3
#define BOARD_LCD_PIN_DE            GPIO_NUM_5
#define BOARD_LCD_PIN_PCLK          GPIO_NUM_7
#define BOARD_LCD_PIN_DISP_EN       GPIO_NUM_NC

// data_gpio_nums[0..15] 순서 (B3..B7, G2..G7, R3..R7)
#define BOARD_LCD_PIN_DATA0         GPIO_NUM_14  // B3
#define BOARD_LCD_PIN_DATA1         GPIO_NUM_38  // B4
#define BOARD_LCD_PIN_DATA2         GPIO_NUM_18  // B5
#define BOARD_LCD_PIN_DATA3         GPIO_NUM_17  // B6
#define BOARD_LCD_PIN_DATA4         GPIO_NUM_10  // B7
#define BOARD_LCD_PIN_DATA5         GPIO_NUM_39  // G2
#define BOARD_LCD_PIN_DATA6         GPIO_NUM_0   // G3 (strapping pin)
#define BOARD_LCD_PIN_DATA7         GPIO_NUM_45  // G4 (strapping pin)
#define BOARD_LCD_PIN_DATA8         GPIO_NUM_48  // G5
#define BOARD_LCD_PIN_DATA9         GPIO_NUM_47  // G6
#define BOARD_LCD_PIN_DATA10        GPIO_NUM_21  // G7
#define BOARD_LCD_PIN_DATA11        GPIO_NUM_1   // R3
#define BOARD_LCD_PIN_DATA12        GPIO_NUM_2   // R4
#define BOARD_LCD_PIN_DATA13        GPIO_NUM_42  // R5
#define BOARD_LCD_PIN_DATA14        GPIO_NUM_41  // R6
#define BOARD_LCD_PIN_DATA15        GPIO_NUM_40  // R7

// ---------------------------------------------------------------------------
// I2C bus (GT911 터치 + CH422G IO 확장기 공유)
// ---------------------------------------------------------------------------
#define BOARD_I2C_PORT              0
#define BOARD_I2C_PIN_SDA           GPIO_NUM_8
#define BOARD_I2C_PIN_SCL           GPIO_NUM_9
#define BOARD_I2C_FREQ_HZ           400000

// GT911: INT 는 GPIO, RST 는 CH422G EXIO1.
// 리셋 중 INT=LOW 로 유지하면 I2C 주소가 0x5D 로 고정된다.
#define BOARD_TOUCH_PIN_INT         GPIO_NUM_4
#define BOARD_TOUCH_I2C_ADDR        0x5D

// ---------------------------------------------------------------------------
// CH422G IO expander (I2C 명령 주소 방식, 레지스터 없음)
// ---------------------------------------------------------------------------
#define CH422G_ADDR_WR_SET          0x24   // 시스템 설정 (bit0 IO_OE: IO0~7 출력 모드)
#define CH422G_ADDR_WR_IO           0x38   // EXIO0~7 출력 값
#define CH422G_ADDR_RD_IO           0x26   // EXIO0~7 입력 값

#define BOARD_EXIO_TP_RST           (1u << 1)  // GT911 reset (active low)
#define BOARD_EXIO_LCD_BL           (1u << 2)  // 백라이트 ON/OFF (PWM 불가)
#define BOARD_EXIO_LCD_RST          (1u << 3)  // LCD reset (active low)
#define BOARD_EXIO_SD_CS            (1u << 4)  // SD 카드 CS (active low, 상시 LOW 유지)
#define BOARD_EXIO_USB_SEL          (1u << 5)  // USB/CAN 선택 (Waveshare 기본값 HIGH 유지)

// 부팅 시 초기값: BL on, LCD_RST high, USB_SEL high, TP_RST low(리셋 중), SD_CS low
#define BOARD_EXIO_BOOT_STATE       (BOARD_EXIO_LCD_BL | BOARD_EXIO_LCD_RST | BOARD_EXIO_USB_SEL)

// ---------------------------------------------------------------------------
// micro SD (SPI2, CS 는 CH422G 로 상시 선택 → sdspi 에는 CS 미지정)
// ---------------------------------------------------------------------------
#define BOARD_SD_SPI_HOST           SPI2_HOST
#define BOARD_SD_PIN_MOSI           GPIO_NUM_11
#define BOARD_SD_PIN_SCLK           GPIO_NUM_12
#define BOARD_SD_PIN_MISO           GPIO_NUM_13
#define BOARD_SD_MOUNT_POINT        "/sdcard"

// ---------------------------------------------------------------------------
// 기타: GPIO19/20 = USB D-/D+, GPIO43/44 = UART0(RS485 공유) → 사용 금지
// ---------------------------------------------------------------------------
