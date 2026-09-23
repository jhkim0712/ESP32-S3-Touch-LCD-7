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
│       ├── ui_core.c            esp_lvgl_port 초기화, APP_EVENT → lv_subject 갱신, 모드 전환
│       ├── ui_theme.c           색상, 카드 스타일, 숫자 포맷
│       ├── status_bar.c         Wi-Fi/BLE/날짜·시각, 모드 전환 버튼 (lv_layer_top)
│       ├── widget_view.c        카드 그리드 (가로 2x2 / 세로 1x4)
│       ├── slide_view.c         lv_tileview 가로 스와이프 + 화살표 + 페이지 점
│       ├── popup_ota.c          업데이트 알림 → 진행률 → 결과
│       └── pages/
│           ├── page_clock.c            아날로그(lv_scale) + 디지털
│           ├── page_photo.c            PSRAM RGB565 프레임 표시, 좌/우 탭으로 이전/다음
│           ├── page_weather.c
│           ├── page_stocks.c
│           ├── page_music.c
│           └── page_settings.c         (5단계) Wi-Fi 스캔·키보드, 회전, 간격
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
            └─ ui_core 핸들러 → lv_async_call (LVGL 태스크로 이동) → lv_subject 갱신
                  └─ 화면에 있는 날씨 위젯의 observer 가 app_state_get_weather() 로 다시 그림
```

- 위젯은 생성될 때 subject 에 observer 를 등록하고, 삭제되면 자동으로 해제됩니다. 그래서 위젯형과 슬라이드형 화면을 바꿔 만들어도 등록/해제 코드가 따로 필요 없고, 데이터도 다시 요청하지 않습니다.
- 반대로 UI 버튼(음악 제어, 새로고침, 사진 이전/다음, OTA 시작)은 `APP_EVT_REQ_*` 이벤트만 보냅니다. UI는 서비스 구현을 직접 호출하지 않습니다.

## 2.6 화면 회전 (5단계, NVS 설정)

설정 페이지에서 0° / 90° / 180° / 270°를 선택하면 NVS에 저장하고, **재부팅할 때 적용**합니다. 렌더링 버퍼 구성이 달라서 실행 중에는 바꾸지 않습니다.

| 회전 | 해상도 | 렌더링 방식 |
|---|---|---|
| 0°, 180° | 800x480 | 지금과 같음: PSRAM 프레임버퍼 2개에 직접 그리기 + 찢어짐 방지. 180°는 LVGL 회전 사용 |
| 90°, 270° | 480x800 | RGB 패널에 하드웨어 회전이 없음 → LVGL 부분 버퍼 + 소프트웨어 회전 후 프레임버퍼에 복사. 찢어짐 방지 미적용, 프레임 속도 저하 |

- **터치:** `lv_display_set_rotation()`을 쓰면 LVGL이 터치 좌표도 함께 변환하므로 GT911 설정은 그대로 둡니다.
- **UI 레이아웃(4단계):** 좌표를 고정하지 않고 flex/grid로 만들어 가로와 세로 모두에서 동작하게 합니다.

## 2.7 한글 폰트와 언어 설정 (5-6)

- **폰트:** LVGL Tiny TTF 로 Noto Sans KR 서브셋 TTF 를 LittleFS(`/storage/fonts`)에서 읽어 런타임에 렌더링합니다.
  한 파일로 14/20/28/48px 크기를 모두 만들고, 글리프 캐시는 PSRAM 에 둡니다. 곡명처럼 임의의 한글이 나오므로
  완성형 2,350자가 아니라 현대 한글 11,172자 전체 + 라틴 문자를 포함합니다 (약 1.5~2MB).
- **기호:** LVGL 기호(`LV_SYMBOL_*`, 와이파이·재생 버튼 등)는 Montserrat 에 있으므로 한글 폰트의 fallback 으로 연결합니다.
- **언어 설정:** 설정 페이지에 한국어 / English 선택을 추가하고 NVS 에 저장합니다. UI 문자열은 언어별 표로 관리하고,
  바꾸면 현재 화면을 다시 만들어 바로 적용합니다(재부팅 불필요). 날짜 형식("9월 23일 (수)"), 날씨 상태 문구도 함께 바뀝니다.
- **날씨 아이콘:** 상태별 색 원 대신 PNG 아이콘을 LittleFS 에 넣어 표시합니다.

## 2.8 단계별 구현 로드맵

| 단계 | 산출물 |
|---|---|
| 3 | `board/*` 구현, `ui_core` LVGL 바인딩 (PSRAM 프레임버퍼 + bounce buffer + GT911) |
| 4 | 테마, `widget_view`, `slide_view`, 모드 전환, 각 페이지 레이아웃 |
| 5-1 | `storage`: NVS 설정(Wi-Fi, UI 모드, 화면 회전, 사진 간격, 종목), 설정 페이지 |
| 5-2 | `net`: Wi-Fi 연결/스캔, SNTP |
| 5-3 | `services`: 날씨, 주가 (`net_worker` 에서 HTTPS 직렬 처리) |
| 5-4 | `photo`: 전자앨범 (ROM TJpgDec 스트리밍 디코딩, PNG, EXIF 방향) |
| 5-5 | `media_ble`: BLE 음악 리모컨 (iOS AMS / Android HID 미디어 키) |
| 5-6 | 한글 폰트 + 언어 설정 + 날씨 아이콘 (아래 2.7 참고) |
| 6 | `ota` + GitHub Actions 릴리스 워크플로 |
