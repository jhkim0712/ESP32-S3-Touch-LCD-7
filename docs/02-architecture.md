# 2단계 — 프로젝트 폴더 구조 설계

## 2.1 디렉터리 구조

```
ESP32-S3-Touch-LCD-7/
├── CMakeLists.txt              # IDF 프로젝트 루트
├── version.txt                 # 펌웨어 버전 → esp_app_desc.version (OTA 비교 기준)
├── sdkconfig.defaults          # PSRAM/LCD/TLS/BLE/LVGL 기본 설정
├── partitions.csv              # 16MB: nvs | otadata | ota_0 6MB | ota_1 6MB | littlefs 3.9MB
├── partitions_8MB.csv          # 8MB 모듈용 (ota 3MB x2, littlefs 1.9MB)
├── dependencies.lock           # (첫 빌드 후 생성, 커밋)
│
├── main/                       # 초기화 순서만 담당
│   ├── main.cpp
│   ├── Kconfig.projbuild       # menuconfig: Wi-Fi, NTP, 좌표, 종목, OTA 저장소
│   └── idf_component.yml
│
├── components/
│   ├── board/        [Display·Storage HW]  ── 하드웨어 의존 코드는 여기에만 존재
│   │   ├── include/board_pins.h   핀·타이밍·CH422G 비트 정의
│   │   ├── include/board.h        board_init(), backlight, sdcard
│   │   ├── ch422g.c               IO 확장기
│   │   ├── board_lcd.c            esp_lcd RGB 패널 + bounce buffer
│   │   ├── board_touch.c          GT911 리셋 시퀀스 + esp_lcd_touch
│   │   └── board_sdcard.c         SPI2 + FATFS 마운트
│   │
│   ├── app_core/     [공통]  공유 상태 모델 + APP_EVENT 이벤트 루프
│   │   └── app_state.c
│   │
│   ├── storage/      [Storage]  LittleFS 마운트, NVS 설정 (Kconfig 기본값 → NVS 덮어쓰기)
│   │   ├── app_storage.c
│   │   └── settings.c
│   │
│   ├── net/          [Network]  wifi_mgr.c · time_sync.c(SNTP/KST) · http_util.c(HTTPS→PSRAM)
│   │
│   ├── services/     [Network]  weather.c(Open-Meteo) · stocks.c(Yahoo/AV) · net_worker.c
│   │
│   ├── media_ble/    [BLE]      ble_core.c(NimBLE, bonding) · ams_client.c(iOS) · hid_media.c(Android)
│   │
│   ├── photo/        [Storage]  photo_loader.c  JPG/PNG → RGB565 PSRAM 더블 버퍼
│   │
│   ├── ota/          [OTA]      github_ota.c  Releases API, semver, esp_https_ota, rollback
│   │
│   └── ui/           [UI]
│       ├── ui_core.cpp          esp_lvgl_port 초기화, 이벤트 → UI 디스패치
│       ├── ui_theme.cpp         색상, 폰트(Montserrat + Tiny TTF 한글)
│       ├── status_bar.cpp       Wi-Fi/BLE/시간, 모드 전환 버튼
│       ├── widget_view.cpp      2x2 카드 그리드
│       ├── slide_view.cpp       lv_tileview 가로 스와이프
│       └── pages/
│           ├── page_clock.cpp          아날로그(lv_scale) + 디지털
│           ├── page_photo.cpp
│           ├── page_weather_stock.cpp
│           ├── page_music.cpp
│           ├── page_settings.cpp       Wi-Fi 스캔·키보드, 모드, 간격
│           └── popup_ota.cpp
│
├── assets/                     # → LittleFS 이미지로 굽기 (littlefs_create_partition_image)
│   ├── fonts/NotoSansKR-subset.ttf
│   ├── icons/weather/*.png
│   └── photos/default.jpg
├── tools/                      # 폰트 서브셋 스크립트, 사진 리사이즈 스크립트
├── .github/workflows/release.yml   # (6단계) 태그 push → 빌드 → Release asset 업로드
└── docs/
```

각 컴포넌트는 `include/` 에 공개 헤더만 노출하며, 헤더에 인터페이스가 이미 정의되어 있습니다. 구현 파일은 단계별로 추가합니다.

## 2.2 의존성 규칙 (단방향)

```
            main
              │
              ▼
             ui ─────────────┬──────────┬───────────┬────────┐
              │              │          │           │        │
              ▼              ▼          ▼           ▼        ▼
            board        services   media_ble     photo     ota
              ▲              │          │           │        │
              │              ▼          │           │        │
              │             net ◄───────┼───────────┼────────┘
              │              │          │           │
              └──────────────┴──► app_core ◄────────┘   storage
```

- **서비스는 UI를 모릅니다.** 결과는 `app_state_set_*()` + `APP_EVENT` 발행으로만 전달합니다.
- **UI만 LVGL을 호출합니다.** 이벤트 핸들러에서 `lvgl_port_lock()` 을 잡거나 `lv_async_call()` 로 LVGL 태스크에 넘깁니다.
- **하드웨어 핀은 `board_pins.h` 에만** 있습니다. 보드를 바꿀 때는 `board/` 만 교체하면 됩니다.

## 2.3 태스크 / 코어 배치 (5단계에서 구현)

| 태스크 | 코어 | 우선순위 | 스택 | 역할 |
|---|---|---|---|---|
| Wi-Fi / lwIP (시스템) | 0 | 23/18 | — | |
| NimBLE host (시스템) | 0 | 21 | — | AMS/HID |
| `net_worker` | 0 | 5 | 8 KB (PSRAM 불가: TLS) | NTP 확인 → 날씨 → 주가 → OTA 확인을 **직렬 처리** |
| `ota_task` | 0 | 4 | 8 KB | 승인 후 다운로드·플래시 쓰기 |
| `lvgl` (esp_lvgl_port) | 1 | 4 | 8 KB | 렌더링, 입력, 타이머(시계 1초 갱신) |
| `photo_loader` | 1 | 2 | 6 KB | 파일 읽기, JPEG 디코딩(LVGL 유휴 시간 사용) |

**HTTPS를 한 태스크에서 직렬 처리하는 이유:** TLS 세션 하나는 내부 RAM 수십 KB를 씁니다. RGB bounce buffer(약 32KB), Wi-Fi, BLE와 함께 동시에 여러 세션을 열면 내부 RAM이 부족해집니다.

## 2.4 메모리 계획 (8MB PSRAM)

| 항목 | 위치 | 크기 |
|---|---|---|
| LCD 프레임버퍼 x2 (800×480×2B) | PSRAM | 1.5 MB |
| Bounce buffer (800×10 줄 ×2) | 내부 SRAM | 32 KB |
| LVGL 힙·객체 | PSRAM (CLIB malloc) | ~1 MB |
| 사진 디코드 더블 버퍼 | PSRAM | 1.5 MB |
| 한글 TTF + 글리프 캐시 | PSRAM | ~2 MB |
| HTTP 응답 / JSON | PSRAM | ~200 KB |
| 여유 | | ~1.5 MB |

## 2.5 데이터 흐름 예시 (날씨)

```
net_worker(core0) ─ http_get_alloc(open-meteo) ─ cJSON 파싱 ─ app_state_set_weather()
      └─ esp_event_post(APP_EVENT, APP_EVT_WEATHER_UPDATED)
            └─ ui_core 핸들러 → lvgl_port_lock() → widget/slide 두 뷰의 날씨 라벨 갱신
```

위젯형과 슬라이드형 뷰는 같은 `app_state` 를 구독하므로, 모드를 바꿔도 데이터를 다시 요청하지 않습니다.

## 2.6 단계별 구현 로드맵

| 단계 | 산출물 |
|---|---|
| 3 | `board/*` 구현, `ui_core` LVGL 바인딩 (PSRAM 프레임버퍼 + bounce buffer + GT911) |
| 4 | 테마, `widget_view`, `slide_view`, 모드 전환, 각 페이지 레이아웃 |
| 5 | `app_core`, `storage`, `net`, `services`, `media_ble`, `photo` + FreeRTOS 태스크 |
| 6 | `ota` + GitHub Actions 릴리스 워크플로 |
