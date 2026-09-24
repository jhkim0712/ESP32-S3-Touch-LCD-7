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
│   ├── services/     [Network]  weather.c(OpenWeatherMap / Open-Meteo) · stocks.c(Yahoo/AV) · net_worker.c
│   │
│   ├── media_ble/    [BLE]      ble_core.c(NimBLE, bonding) · ams_client.c(iOS) · hid_media.c(Android)
│   │
│   ├── photo/        [Storage]  photo_loader.c  JPG/PNG → RGB565 PSRAM 더블 버퍼
│   │
│   ├── ota/          [OTA]      github_ota.c  Releases API, semver, esp_https_ota, rollback (net_worker 에서 호출)
│   │
│   ├── photo_feed/   [Network]  photo_feed.c  사진 RSS/Atom 피드 → SD 미러 (net_worker 에서 호출)
│   │
│   ├── web/          [Network]  web_server.c  esp_http_server + REST API + mDNS, www/index.html (gzip 내장)
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
| `net_worker` | 0 | 5 | 10 KB (PSRAM 불가: TLS) | 날씨 → 주가 → 업데이트 확인 → 사진 피드 동기화를 **직렬 처리**. 승인된 OTA 설치(다운로드·플래시 쓰기)도 여기서 실행 |
| `lvgl` (esp_lvgl_port) | 1 | 4 | 12 KB (내부 RAM: 폰트 파일 flash 읽기) | 렌더링, 입력, 타이머(시계 1초 갱신). 한글 글리프를 처음 그릴 때 약 7.7KB 사용 |
| `photo_loader` | 1 | 2 | 6 KB | 파일 읽기, JPEG 디코딩(LVGL 유휴 시간 사용) |
| `httpd` (웹 설정) | 0 | 5 | 6 KB (내부 RAM: NVS 쓰기) | 웹 페이지, REST API. 동시 연결 3개 |
| `mdns` | 0 | 1 | 4 KB (PSRAM) | `smart-display.local` 응답 |

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

## 2.8 웹 UI, 사진 업로드, 사진 피드 동기화 (5-7 ~ 5-9)

같은 공유기에 연결된 PC/휴대폰 브라우저에서 `http://smart-display.local` (mDNS) 또는 IP 로 접속합니다.

**웹 서버 (5-7, 구현됨: `components/web`)**
- `esp_http_server` (HTTP, 같은 네트워크 전용) + `espressif/mdns`. 서버 태스크는 core 0, 동시 연결은 3개
  (넘치면 가장 오래된 연결을 닫음). mDNS 태스크와 버퍼는 PSRAM 에 둡니다.
- 페이지는 `www/index.html` 한 파일(HTML/CSS/JS, 한국어/English)입니다. 빌드할 때 gzip 으로 압축해 **펌웨어에 내장**합니다.
  처음 계획은 LittleFS 였지만, OTA 는 앱 파티션만 바꾸므로 LittleFS 에 두면 업데이트 뒤 페이지와 API 버전이 어긋날 수 있습니다.
- 설정은 JSON REST API 로 주고받습니다: `GET /api/status`, `GET/POST /api/settings`, `POST /api/wifi/scan`, `POST /api/restart`.
  `POST /api/settings` 는 보낸 항목만 바꾸고, Wi-Fi 비밀번호는 읽을 수 없습니다(설정 여부만 반환).
  저장하면 `APP_EVT_SETTINGS_CHANGED` (data: `APP_SETTINGS_SRC_WEB`) 를 보내고, 서비스는 기기 설정 화면과 같은 방식으로 다시 읽습니다.
  UI 는 언어와 화면 모드를 바로 적용하고, 기기 설정 화면이 열려 있으면 이전 값으로 다시 저장되지 않도록 홈으로 돌아갑니다.
- 접근 보호: 모든 `/api` 요청에 `X-PIN` 헤더가 필요합니다. PIN(6자리)은 처음 부팅할 때 무작위로 만들어 NVS 에 저장하고,
  기기 설정 화면의 "웹 설정" 항목에 주소와 함께 표시합니다. 브라우저는 PIN 을 localStorage 에 기억합니다.
  5번 틀리면 30초 동안 모든 요청을 거부합니다. HTTP 이므로 같은 네트워크 안에서 사용하는 것을 전제로 합니다.

**사진 업로드 + 자르기 (5-8, 구현됨: `web/web_photos.c`, 웹 페이지 "사진" 탭)**
- 여러 파일을 한 번에 선택하면 브라우저가 한 장씩 자르기 화면을 보여 줍니다.
  비율: 화면 가로(800x432), 화면 세로(480x752), 자유, 전체. 이 크기는 상태 표시줄을 뺀 기기의 사진 영역이라 기기에서 다시 줄이지 않습니다.
  [나머지는 자동]을 누르면 남은 사진은 사진 방향에 맞는 화면 비율로 가운데를 잘라 올립니다.
- **자르기와 크기 조정, JPEG 인코딩을 브라우저(canvas)에서 처리**한 뒤 baseline JPEG 로 올립니다.
  → 기기에서 못 푸는 progressive JPEG, 큰 PNG, 아이폰 HEIC(사파리에서 열 때) 문제가 업로드 단계에서 해결되고, SD 에서 읽는 시간도 짧아집니다.
  EXIF 방향은 브라우저가 적용해 그립니다.
- 업로드는 한 파일씩 순서대로 `POST /api/photos/upload?album=&name=` 로 SD 에 바로 기록 (`.part` 임시 파일 → 완료 후 이름 변경,
  같은 이름이 있으면 `이름-1.jpg`). 이어서 240px 썸네일을 `<앨범>/.thumbs/` 에 올립니다 (전자앨범은 `.` 폴더를 건너뜀).
- 폴더 탐색: `album` 은 사진 폴더 기준 상대 경로(`a/b`, 전자앨범과 같은 4단계까지). 경로 표시줄과 폴더 타일로 이동하고,
  새 폴더(첫 사진을 올릴 때 만들어짐), 썸네일 보기(60장씩), 삭제. 마지막 사진을 지우면 빈 폴더도 지웁니다.
  `feeds` 폴더(사진 피드 미러)와 그 아래는 볼 수만 있습니다. 경로가 길어 `CONFIG_HTTPD_MAX_URI_LEN=1024`.
  PC 에서 직접 넣은 사진은 썸네일이 없어 원본을 받아 표시합니다.
- 업로드/삭제 후 3초 동안 더 바뀌지 않으면 `APP_EVT_REQ_PHOTO_RESCAN` → `photo_loader` 가 목록을 다시 읽습니다 (표시 중인 사진은 유지).
- 전자앨범이 읽고 있는 파일을 지우지 않도록 FatFs 파일 잠금(`CONFIG_FATFS_FS_LOCK`)을 켰습니다. 이때 삭제는 `409 busy` 가 되고 브라우저가 잠시 후 다시 시도합니다.

**사진 피드 동기화 (5-9, 구현됨: `components/photo_feed`, 웹 페이지 "사진" 탭의 "사진 피드 (RSS)" 카드)**

처음에는 다른 비슷한 프로젝트(480x320 보드)의 `app_flickr` 를 옮겨 Flickr 피드만 받았고,
v0.2.3 에서 일반 RSS 2.0 / Atom 이미지 피드를 받도록 넓히면서 이름을 `photo_feed` 로 바꿨습니다.

- 동작: 웹 UI 에서 피드 URL 을 추가하면 주기적으로(`CONFIG_APP_FEED_SYNC_MIN`, 기본 1시간, 실패 시 10분 후) 피드를 받아
  항목마다 이미지 하나를 `/sdcard/photos/feeds/<피드 URL 해시>/` 에 저장합니다. 전자앨범은 하위 폴더를 이미 탐색하므로 다른 사진과 함께 표시됩니다.
  예: Flickr `https://www.flickr.com/services/feeds/photos_public.gne?id=<사용자 ID>&format=rss2`
- 폴더는 피드의 **미러**입니다: 피드에서 빠진 이미지는 지우고, 피드를 삭제하면 그 폴더도 지웁니다.
  그래서 웹 사진 관리에서는 `feeds` 폴더를 보기만 하고, 그 안으로 올리거나 지우는 요청은 거부합니다.
- 항목(`<item>` / `<entry>`)의 이미지 찾기, 먼저 찾은 것 사용:
  `<media:content url>` → `<enclosure url>` → Atom `<link rel="enclosure" href>` (셋 다 `type`/`medium` 이 이미지가 아니면 건너뜀)
  → `<media:thumbnail url>` → 본문(`<description>`, `<content:encoded>`) HTML 의 첫 `<img src>` (CDATA 원문 또는 `&lt;img` 로 이스케이프된 HTML).
  주소 끝이 `.gif/.webp/.mp4` 등인 항목은 받지 않습니다.
- 파일 이름: 주소가 `<이름>.jpg/.jpeg/.png` 로 끝나고 피드 안에서 겹치지 않으면 그 이름, 아니면 `img_<URL 해시>`.
  확장자는 받은 파일의 첫 바이트로 정합니다 (JPEG → `.jpg`, PNG → `.png`). JPEG/PNG 가 아니면(HTML 오류 페이지, WebP 등)
  빈 `<이름>.skip` 파일을 남겨 다음 동기화 때 다시 받지 않습니다 (피드에서 빠지면 함께 지움).
- staticflickr.com 의 1024px(`_b`) 주소는 800px(`_c`)로 바꿔 받고, 없으면 원래 주소로 받습니다.
- 받는 중인 파일은 `.part` 로 쓰고 완료 후 이름을 바꿉니다. 피드는 512KB, 이미지는 4MB 까지만 받습니다.
- 동기화는 자체 태스크 없이 `net_worker` 가 날씨/주가 다음에 `photo_feed_sync()` 로 실행합니다 (TLS 세션 1개 유지).
  Wi-Fi 가 끊겨 있으면 `photo_feed_sync(false)`: 네트워크 없이 삭제된 피드의 폴더만 정리합니다.
  바뀐 것이 있으면 `APP_EVT_REQ_PHOTO_RESCAN` → `photo_loader` 가 다시 탐색합니다.
- 피드 목록은 NVS 키 `feeds` (줄바꿈 구분, 최대 8개 x 255자). `app_settings_t` 는 작은 스택에 자주 복사되므로 따로 둡니다.

v0.2.2 (`app_flickr`) 에서 업데이트할 때:
- NVS 키 `flickr` → `feeds` 로 처음 읽을 때 옮깁니다 (`settings_get_photo_feeds`).
- SD 폴더 `photos/flickr` → `photos/feeds` 로 첫 동기화 때 이름을 바꿉니다 (피드 해시와 Flickr 파일 이름이 같아 다시 받지 않음).
- menuconfig `APP_FLICKR_SYNC_MIN` → `APP_FEED_SYNC_MIN` (바꿔 두었다면 다시 설정).

웹 UI / API:
- 피드 목록 추가/삭제, 동기화 상태(진행 중, 마지막 동기화 시각, 사진 수, 표시할 수 없는 사진 수, 건너뛴 항목 수, 오류), [지금 동기화] 버튼
- `GET/POST /api/feeds` (피드 목록 + 상태), `POST /api/feeds/sync`

**제약:** 기기 JPEG 디코더는 progressive JPEG 를 풀지 못합니다. 받은 파일의 헤더(SOF 마커)를 확인해 개수를 웹 UI 에 보여 줍니다.
파일은 남겨 두어(다시 받지 않도록) 전자앨범이 건너뜁니다. WebP/GIF/AVIF 는 지원하지 않습니다.
전자앨범이 읽고 있는 파일은 FatFs 파일 잠금 때문에 지워지지 않고 다음 동기화 때 지워집니다.

## 2.9 단계별 구현 로드맵

| 단계 | 산출물 |
|---|---|
| 3 | `board/*` 구현, `ui_core` LVGL 바인딩 (PSRAM 프레임버퍼 + bounce buffer + GT911) |
| 4 | 테마, `widget_view`, `slide_view`, 모드 전환, 각 페이지 레이아웃 |
| 5-1 | `storage`: NVS 설정(Wi-Fi, UI 모드, 화면 회전, 사진 간격, 종목), 설정 페이지 |
| 5-2 | `net`: Wi-Fi 연결/스캔, SNTP |
| 5-3 | `services`: 날씨, 주가 (`net_worker` 에서 HTTPS 직렬 처리) |
| 5-4 | `photo`: 전자앨범 (ROM TJpgDec 스트리밍 디코딩, PNG, EXIF 방향) |
| 5-5 | `media_ble`: BLE 음악 리모컨 (iOS AMS / Android HID 미디어 키). 내부 RAM 부족으로 기본 비활성(`CONFIG_APP_BLE_MEDIA=n`, `CONFIG_BT_ENABLED=n`) - 코드는 유지, 끄면 음악 카드/페이지·BT 아이콘·설정 항목이 숨겨짐 |
| 5-6 | 한글 폰트 + 언어 설정 + 날씨 아이콘 (위 2.7 참고) |
| 5-7 | 웹 UI: 브라우저에서 설정 (아래 2.8 참고) |
| 5-8 | 웹 UI: 사진 업로드 (여러 장 동시) + 브라우저에서 자르기(crop) |
| 5-9 | `photo_feed`: 웹 UI 에 사진 RSS/Atom 피드(Flickr 등) 등록 → `net_worker` 가 주기적으로 이미지를 SD 카드에 저장 → 전자앨범에 표시 (위 2.8 참고) |
| 6 | `ota` + GitHub Actions 릴리스 워크플로 (웹 UI 에서도 업데이트 확인/실행, 아래 2.10 참고) |

## 2.10 GitHub Release OTA (6단계, 구현됨: `components/ota`, `.github/workflows/release.yml`)

**릴리스 만들기**
1. `version.txt` 를 새 버전으로 올리고 커밋합니다 (로컬 빌드의 버전. CI 는 태그에서 덮어씀).
2. `git tag v0.2.0 && git push origin v0.2.0`
3. GitHub Actions 가 ESP-IDF v6.1 로 빌드하고 Release 를 만들어 `smart_display.bin`(OTA 용)과
   처음 USB 로 굽는 데 필요한 파일(bootloader, partition-table, ota_data_initial, storage)을 올립니다.

**기기 동작**
- `net_worker` 가 Wi-Fi 연결 직후와 `CONFIG_APP_OTA_CHECK_INTERVAL_H`(기본 6시간)마다
  `api.github.com/repos/{owner}/{repo}/releases/latest` 를 확인합니다 (비인증 요청 한도: 시간당 60회). 실패하면 30분 후 다시 확인합니다.
- `tag_name` 을 `esp_app_desc.version` 과 semver 로 비교해 새 버전이면 `APP_EVT_OTA_AVAILABLE` → 기기 팝업 (태그마다 한 번).
  웹 설정 페이지의 "펌웨어 업데이트" 카드에서도 확인/설치할 수 있습니다 (`GET /api/ota`, `POST /api/ota/check`, `POST /api/ota/install`).
- 설치(`APP_EVT_REQ_OTA_START`)도 `net_worker` 에서 `esp_https_ota` 로 실행합니다 → TLS 세션은 여전히 하나이고 별도 태스크 스택이 필요 없습니다.
  설치 중에는 날씨/주가/사진 피드 갱신이 멈추지만 끝나면 재부팅합니다.
  GitHub 은 다운로드를 서명된 긴 URL 로 리다이렉트하므로 HTTP 버퍼를 늘렸습니다 (수신 4KB / 송신 2KB).
- 받은 이미지의 `project_name` 이 다르면 쓰기 전에 거부합니다 (다른 프로젝트 파일을 올린 경우). 칩 종류와 이미지 검증은 `esp_https_ota` 가 합니다.
- 진행률은 `APP_EVT_OTA_PROGRESS` → 기기 팝업(웹에서 시작했으면 진행 창을 새로 띄움)과 웹 진행 막대에 표시됩니다.
- **롤백:** `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`. 새 펌웨어는 부팅 후 60초 동안 정상 동작하면 `esp_ota_mark_app_valid_cancel_rollback()` 으로 확정합니다.
  그 전에 멈추거나 재부팅되면 부트로더가 이전 펌웨어로 되돌립니다.
- **제약:** OTA 는 앱 파티션만 바꿉니다. LittleFS(`assets/`, 폰트)가 바뀐 릴리스는 USB 로 `storage.bin` 을 다시 써야 합니다.
  펌웨어 서명(Secure Boot)은 쓰지 않으므로 저장소에 쓰기 권한이 있는 사람이 올린 Release 를 그대로 믿습니다.

## 2.11 날씨 제공자 (OpenWeatherMap / Open-Meteo)

- 설정 `owm_api_key`, `weather_city` (NVS, 기기 설정 화면과 웹 설정 페이지에서 입력, 기본값은 menuconfig).
- API 키가 있으면 OpenWeatherMap `data/2.5/weather` (units=metric): 도시가 숫자면 `id=`, 아니면 `q=이름,국가`, 비어 있으면 menuconfig 좌표.
  응답의 날씨 코드(2xx~8xx)는 UI 아이콘/문구가 쓰는 WMO 코드로 바꾸고, 아이콘 이름의 `d`/`n` 으로 낮/밤을 정합니다.
  도시 이름(`name`)은 날씨 카드 제목에 표시합니다 (서울 등 주요 도시는 한국어로).
- 키가 없으면 기존처럼 Open-Meteo (키 불필요).
- 키나 도시를 바꾸면 `net_worker` 가 바로 다시 받습니다. 결과(정상, 키 오류, 도시 없음 등)는 웹 상태 카드에 표시합니다.
  웹은 API 키를 돌려주지 않고 설정 여부와 끝 4자리만 보여 줍니다.

