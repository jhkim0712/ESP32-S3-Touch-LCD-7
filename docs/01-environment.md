# 1단계 — 개발 환경 세팅 및 라이브러리 구성

## 1.1 프레임워크 선택: **ESP-IDF v6.1**

| 판단 기준 | ESP-IDF | Arduino-ESP32 v3.x |
|---|---|---|
| RGB LCD 안정성 설정 (`SPIRAM_XIP_FROM_PSRAM`, `LCD_RGB_ISR_IRAM_SAFE`, bounce buffer) | sdkconfig 로 직접 제어 | 미리 빌드된 라이브러리라 sdkconfig 변경 불가 |
| OTA 중 화면 | Flash 쓰기 중에도 PSRAM XIP 로 화면 유지 | Flash 쓰기 때마다 화면이 흔들리거나 밀림(drift) |
| BLE 스택 | NimBLE 선택 가능 (Bluedroid 대비 RAM 약 50% 절감) | Bluedroid 기본 (3.x 에서 NimBLE 선택 가능해졌으나 제약 있음) |
| TLS 메모리 | mbedTLS 를 PSRAM 에 할당, 동적 버퍼 | 기본값 고정 |
| 파티션·롤백 | 커스텀 파티션, `APP_ROLLBACK` | 가능하지만 번거로움 |
| 학습 곡선 | 높음 | 낮음 |

**선택 이유:** 800x480 RGB 패널은 CPU 개입 없이 PSRAM 프레임버퍼를 계속 스캔아웃합니다. 이 구조에서는 Wi-Fi·TLS·BLE·SD·OTA(Flash 쓰기)가 동시에 돌 때 PSRAM 대역폭과 캐시 비활성화가 화면 흔들림의 주원인입니다. 이를 막는 설정(`CONFIG_SPIRAM_XIP_FROM_PSRAM`, `CONFIG_LCD_RGB_ISR_IRAM_SAFE`, bounce buffer)은 Arduino 코어에서 바꿀 수 없어 ESP-IDF를 선택했습니다. 요구사항에 있는 "백그라운드 OTA 중 화면 유지"를 충족하려면 사실상 이 설정들이 필요합니다.

> Arduino 라이브러리를 꼭 써야 하면 `espressif/arduino-esp32` 를 IDF 컴포넌트로 추가하는 방법(Arduino as component)이 있습니다. 다만 이 프로젝트에 필요한 기능은 모두 IDF 네이티브 API로 대체할 수 있어 추가하지 않았습니다.

### 설치 (Windows)
1. **ESP-IDF Installation Manager(EIM)** 로 **v6.1** 설치 (기본 경로: `C:\esp\v6.1\esp-idf`, 도구: `C:\Espressif\tools`)
2. VS Code 사용 시 확장 **"ESP-IDF" (Espressif)** 설치 후 v6.1 경로 지정
3. PowerShell에서 (EIM 설치는 `export.ps1`이 도구를 찾지 못하므로 EIM이 만든 프로필 스크립트를 사용):
   ```powershell
   . C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
   cd E:\Projects\ESP32-S3-Touch-LCD-7
   idf.py set-target esp32s3      # sdkconfig.defaults 적용
   idf.py menuconfig              # "Smart Display Configuration" 에서 Wi-Fi 등 입력
   idf.py build
   idf.py -p COMx flash monitor   # 보드의 USB(UART) 포트
   ```
4. **Flash 용량 확인** (중요): `esptool.py -p COMx flash_id`
   - `16MB` → 기본 `partitions.csv` 사용 (이 보드에서 확인됨)
   - `8MB` → `sdkconfig.defaults` 에서 `FLASHSIZE_8MB`, `partitions_8MB.csv` 로 변경

> 실제 보드 부팅 로그(`Detected size(16384k)`)로 **16MB Flash (N16R8) / 8MB Octal PSRAM** 임을 확인했습니다. 8MB 모듈용 파티션(`partitions_8MB.csv`)도 함께 둡니다.

---

## 1.2 하드웨어 매핑 (`components/board/include/board_pins.h`)

### RGB LCD (ST7262, RGB565 16-bit)
| 신호 | GPIO | | 신호 | GPIO |
|---|---|---|---|---|
| HSYNC | 46 | | PCLK | 7 |
| VSYNC | 3 | | DE | 5 |
| B3 B4 B5 B6 B7 | 14 38 18 17 10 | | DISP_EN | 없음 |
| G2 G3 G4 G5 G6 G7 | 39 0 45 48 47 21 | | | |
| R3 R4 R5 R6 R7 | 1 2 42 41 40 | | | |

타이밍: PCLK 16 MHz, HSYNC pulse/back/front = 4/8/8, VSYNC = 4/8/8, `pclk_active_neg = 1`

### I2C 버스 (공유) — SDA 8, SCL 9, 400 kHz
| 장치 | 주소 | 비고 |
|---|---|---|
| GT911 터치 | 0x5D | INT = GPIO4, RST = CH422G EXIO1 |
| CH422G IO 확장기 | 0x24 (설정), 0x38 (출력), 0x26 (입력) | 레지스터 방식이 아니라 **주소 자체가 명령** |

### CH422G 출력 비트
| EXIO | 기능 | 부팅 값 |
|---|---|---|
| 1 | CTP_RST (GT911 리셋) | LOW → HIGH |
| 2 | DISP (백라이트 부스트 EN) | LOW → 첫 화면을 그린 뒤 HIGH (ON/OFF만 가능, 밝기 조절 불가) |
| 3 | LCD_RST | HIGH |
| 4 | SDCS | LOW (상시 선택) |
| 5 | USB_SEL (GPIO19/20을 USB 또는 CAN으로 전환) | HIGH (Waveshare 기본값) |
| 6 | LCD_VDD_EN (패널 전원 승압 EN) | HIGH (회로도 V1.2에서 확인) |

### 초기화 순서 (`components/board/board.c`)
```
CH422G[0x24] ← 0x01          // IO0~7 출력 모드
CH422G[0x38] ← 0x68          // LCD_VDD_EN, USB_SEL, LCD_RST high / TP_RST low / 백라이트 OFF
TP_RST low, GPIO4 ← LOW      // INT low 상태에서 리셋 해제 → GT911 주소 0x5D
TP_RST high, 60ms 대기
GPIO4 → input, 50ms 대기
RGB 패널 생성 → GT911 드라이버 → LVGL → 첫 화면 → 백라이트 ON
```

### microSD (SPI2)
MOSI 11, SCLK 12, MISO 13, CS = CH422G EXIO4 (상시 LOW) → `sdspi` 설정에서 `gpio_cs = GPIO_NUM_NC`

### 사용하면 안 되는 핀
GPIO19/20 (USB 또는 CAN), GPIO43/44 (UART0 콘솔), GPIO15/16 (RS485). GPIO0/45/46 은 LCD 데이터로 쓰이는 스트래핑 핀입니다. 남는 핀은 Sensor AD 헤더의 GPIO6 정도입니다.

---

## 1.3 라이브러리 구성

요구사항에 적힌 Arduino 라이브러리를 ESP-IDF에서는 아래로 대체합니다.

| 용도 | 요구사항 예시 | 채택 | 출처 |
|---|---|---|---|
| GUI | LVGL | **lvgl/lvgl ^9.3** (현재 9.5, esp_lvgl_port 2.9가 IDF 6에서 9.3 이상 필요) | Component Registry |
| LVGL 포팅 | — | **espressif/esp_lvgl_port ^2.4** (태스크, lock, RGB bounce buffer, tearing 방지) | Registry |
| 터치 | — | **espressif/esp_lcd_touch_gt911** | Registry |
| RGB LCD | — | `esp_lcd` (`esp_lcd_new_rgb_panel`) | IDF 내장 |
| IO 확장기 | — | 자체 CH422G 드라이버 (약 40줄, `i2c_master`) | 직접 작성 |
| JSON | ArduinoJson | **espressif/cjson** (IDF 6부터 본체에서 분리) | Registry |
| Wi-Fi 설정 | WiFiManager | `esp_wifi` + **화면 키보드 설정 페이지** + NVS 저장 | IDF 내장 |
| HTTPS | HTTPClient | `esp_http_client` + `esp_crt_bundle` | IDF 내장 |
| NTP | configTime | `esp_netif_sntp` + `TZ=KST-9` | IDF 내장 |
| BLE | ESP32-BLE-Arduino | **NimBLE** (`bt` 컴포넌트) | IDF 내장 |
| OTA | HTTPUpdate | `esp_https_ota` + `app_update` (rollback) | IDF 내장 |
| JPEG | TJpg_Decoder | **espressif/esp_jpeg** (ROM TJpgDec, RGB565 출력) | Registry |
| PNG | PNGdec | LVGL 내장 `lodepng` | LVGL |
| 내부 파일시스템 | SPIFFS | **joltwallet/littlefs** (SPIFFS는 IDF에서 유지보수만 되는 상태) | Registry |
| SD 카드 | SD | `sdspi` + FATFS | IDF 내장 |
| 한글 폰트 | — | LVGL **Tiny TTF** + Noto Sans KR 서브셋(LittleFS에 보관) | LVGL |

---

## 1.4 요구사항 검토 결과 (구현 전 확인 필요)

1. **음악 — Bluetooth Classic 불가.** ESP32-S3는 BLE 5.0만 지원합니다. A2DP/AVRCP(Classic)는 하드웨어적으로 불가능하므로 다음과 같이 구현합니다.
   - **iOS:** Apple Media Service(AMS)로 곡명·아티스트·재생 상태를 받고 재생/일시정지/이전/다음을 제어합니다. 별도 앱은 필요 없습니다.
   - **Android:** BLE HID Consumer Control(미디어 키)로 제어합니다. **곡 정보는 표준 BLE 서비스가 없어** companion 앱(예: MediaSession을 읽어 커스텀 GATT로 전송)이 있어야 표시할 수 있습니다.
   - 이 보드에는 오디오 코덱/스피커가 없으므로 "음악 플레이어"는 **스마트폰 리모컨**입니다.
   - **보류 — 블루투스 스피커 재생 (SD `mp3/` 폴더, 팟캐스트 RSS):** 블루투스 스피커는 Classic A2DP로 연결되는데 ESP32-S3는 이를 지원하지 않습니다. LE Audio도 BLE 5.2가 필요해 쓸 수 없습니다. 재생하려면 S3가 MP3/AAC를 디코딩하고, 소리는 외부 하드웨어로 내보내야 합니다. 후보는 USB 블루투스 오디오 송신 동글(USB 호스트), Classic BT를 지원하는 보조 ESP32(UART 연결), I2S DAC + 아날로그 송신기입니다. 하드웨어를 정한 뒤 다시 검토합니다.
2. **주가 "실시간".** Yahoo v8 chart API는 비공식이고 키가 필요 없지만 언제든 막힐 수 있습니다(`User-Agent` 헤더 필요). Alpha Vantage 무료 플랜은 **하루 25회**로, 4개 종목이면 약 4시간에 1회만 갱신할 수 있습니다. 그래서 **Yahoo(60초 폴링)를 기본으로** 하고 provider 인터페이스로 교체할 수 있게 설계했습니다. KOSPI = `^KS11`.
3. **날씨.** **Open-Meteo**(API 키 없음, 무료, WMO 날씨 코드)를 기본으로 했습니다. OpenWeatherMap은 키 관리가 필요해 선택 사항으로 둡니다.
4. **한글 표시.** LVGL 기본 폰트(Montserrat)에는 한글이 없습니다. 곡 제목 등 한글을 표시하려면 한글 폰트가 필수입니다. Tiny TTF + 서브셋 TTF(약 1~2MB, LittleFS)로 PSRAM에서 렌더링할 계획입니다.
5. **백라이트 밝기 조절 불가.** CH422G로 ON/OFF만 할 수 있습니다(야간 자동 끄기 정도만 가능).
6. **GitHub API 제한.** 비인증 요청은 IP당 시간당 60회입니다. 6시간 주기 확인이면 충분합니다. private 저장소는 토큰이 필요하므로 **public 저장소 Release를 전제**로 합니다.

## 1.5 핵심 sdkconfig (`sdkconfig.defaults`)
| 설정 | 이유 |
|---|---|
| `SPIRAM_MODE_OCT`, `SPEED_80M` | N8R8 Octal PSRAM |
| `SPIRAM_XIP_FROM_PSRAM` | Flash 쓰기 중에도 화면 유지 (OTA/NVS/LittleFS) |
| `LCD_RGB_ISR_IRAM_SAFE`, `LCD_RGB_RESTART_IN_VSYNC` | 캐시 비활성 시 ISR 동작 보장, 화면 밀림 자동 복구 |
| `DATA_CACHE_64KB`, `LINE_64B` | PSRAM 프레임버퍼 대역폭 |
| `MBEDTLS_EXTERNAL_MEM_ALLOC`, `DYNAMIC_BUFFER` | TLS 세션당 내부 RAM 절약 |
| `SPIRAM_MALLOC_RESERVE_INTERNAL=48KB` | DMA·bounce buffer·Wi-Fi 용 내부 RAM 확보 |
| `BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL` | BLE 스택을 PSRAM 에 |
| `BOOTLOADER_APP_ROLLBACK_ENABLE` | OTA 실패 시 자동 복구 |
| `LV_USE_CLIB_MALLOC` | LVGL 힙을 시스템 malloc(→ 큰 할당은 PSRAM)으로 |
