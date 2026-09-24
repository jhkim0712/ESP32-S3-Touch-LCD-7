# ESP32-S3-Touch-LCD-7 Smart Display

[Waveshare ESP32-S3-Touch-LCD-7](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7)(7인치 800x480 RGB LCD, GT911 터치, 8MB PSRAM)용 스마트 디스플레이 펌웨어입니다.

## 기능
| 기능 | 내용 |
|---|---|
| 시계 | Wi-Fi 연결 후 NTP로 KST(UTC+9) 동기화, 아날로그/디지털 시계 |
| 전자앨범 | SD 카드의 JPG/PNG 슬라이드쇼 (PSRAM에서 미리 디코딩). 웹에서 사진 업로드(자르기)/삭제, Flickr 피드 사진 자동 동기화 |
| 날씨 | Open-Meteo API로 현재 기온, 습도, 날씨 아이콘 표시 |
| 주가 | Yahoo Finance로 AAPL, NVDA, KOSPI(`^KS11`) 등 시세와 등락률 표시 |
| 음악 리모컨 | BLE로 스마트폰 음악 제어 (iOS: AMS / Android: 미디어 키). 내부 RAM 부족으로 **기본 비활성**, menuconfig `CONFIG_APP_BLE_MEDIA` 로 켤 수 있음 |
| OTA | GitHub Releases에서 새 버전을 확인하고, 화면 팝업으로 승인하면 HTTPS로 업데이트 |
| UI | 위젯형(카드 그리드)과 슬라이드형(좌우 스와이프) 두 모드 전환 |
| 웹 설정 | 같은 Wi-Fi의 PC/휴대폰 브라우저에서 `http://smart-display.local` 로 설정 변경 (기기 화면의 PIN 필요) |

## 개발 환경
- ESP-IDF **v6.1 이상**
- LVGL 9.x (현재 9.5) + esp_lvgl_port
- 대상 칩: ESP32-S3 (16MB Flash / 8MB Octal PSRAM)

> **보류:** 블루투스 스피커로 SD 카드 MP3나 팟캐스트(RSS)를 재생하는 기능은 이번 범위에서 뺐습니다. ESP32-S3는 Bluetooth Classic(A2DP)을 지원하지 않아 외부 송신 하드웨어가 필요하기 때문입니다. 자세한 내용은 [docs/01-environment.md](docs/01-environment.md#14-요구사항-검토-결과)에 있습니다.

## 빌드 및 플래시
ESP-IDF를 EIM(ESP-IDF Installation Manager)으로 설치했다면 먼저 환경을 불러옵니다.
```powershell
. C:\Espressif\tools\Microsoft.v6.1.PowerShell_profile.ps1
```

```powershell
idf.py set-target esp32s3
idf.py menuconfig        # "Smart Display Configuration" 메뉴에서 Wi-Fi, 좌표, 종목 등 입력
idf.py build
idf.py -p COMx flash monitor
```

> Windows(한글 로케일)에서는 `sdkconfig.defaults`, `Kconfig.projbuild`, `partitions*.csv`, `CMakeLists.txt`에 **한글을 쓰면 안 됩니다.** IDF 설정 도구가 이 파일들을 cp949로 읽기 때문에 빌드 설정 단계에서 실패합니다.

## 폴더 구조
```
main/            진입점, menuconfig 항목(Kconfig.projbuild)
components/
  board/         핀 매핑, CH422G IO 확장기, RGB LCD, GT911 터치, SD 카드
  app_core/      공유 상태 모델 + 앱 이벤트
  storage/       LittleFS, NVS 설정
  net/           Wi-Fi, SNTP, HTTPS 헬퍼
  services/      날씨, 주가 수집 태스크
  media_ble/     NimBLE AMS / HID 미디어 컨트롤
  photo/         사진 로더 / JPEG 디코더
  ota/           GitHub Release OTA
  ui/            LVGL 화면 (위젯형 / 슬라이드형)
  web/           웹 설정 페이지 (HTTP 서버, REST API, mDNS)
  app_flickr/    Flickr 피드 사진을 SD 카드에 동기화
docs/            설계 문서
```

## 전자앨범 사진 준비
SD 카드 `photos/` 폴더(하위 폴더 4단계까지)에 JPG/PNG 를 넣습니다. 기기의 JPEG 디코더는
baseline JPEG 만 지원하고 PNG 는 약 600x400 이하만 풀 수 있으므로, PC 에서 변환해 넣는 것을 권장합니다.
```powershell
pip install pillow        # 아이폰 HEIC: pip install pillow-heif
python tools/prepare_photos.py  D:\MyPhotos  F:\photos
```

## 웹 설정
기기와 같은 Wi-Fi에 연결된 브라우저에서 `http://smart-display.local` (안 되면 기기 설정 화면에 표시된 IP 주소)로 접속합니다.
처음 접속할 때 기기 설정 화면(톱니바퀴) > **웹 설정**에 표시된 6자리 PIN 을 입력합니다. PIN 은 처음 부팅할 때 무작위로 만들어 NVS 에 저장됩니다.
Wi-Fi, 언어, 화면 모드, 회전, 사진 전환 간격, 주식 종목을 바꿀 수 있고, 기기를 다시 시작할 수도 있습니다.
**사진** 탭에서는 SD 카드 앨범별로 사진을 보고 지울 수 있고, 여러 장을 골라 브라우저에서 자른 뒤(화면 가로/세로/자유) 올릴 수 있습니다.
Flickr 피드 주소를 등록하면 1시간마다 새 사진을 `photos/flickr/` 에 받아 전자앨범에 함께 표시합니다 (이 폴더는 피드와 똑같이 유지되므로 직접 파일을 넣지 마세요).
페이지는 `components/web/www/index.html` 이며 빌드할 때 gzip 으로 압축해 펌웨어에 넣습니다.

## 폰트 (LittleFS)
`assets/` 폴더는 빌드 때 LittleFS 이미지로 만들어져 `idf.py flash` 로 함께 기록됩니다 (storage 파티션을 덮어씀).
한글 폰트(Noto Sans KR 서브셋)와 날씨 아이콘 폰트(Weather Icons)는 `tools/make_fonts.py` 로 다시 만들 수 있습니다
(`pip install fonttools`). 두 폰트 모두 SIL OFL 1.1 이며 라이선스 파일이 `assets/fonts/` 에 있습니다.

## 문서
- [1단계 — 개발 환경 및 라이브러리](docs/01-environment.md)
- [2단계 — 폴더 구조 및 아키텍처](docs/02-architecture.md)

## 진행 상황
- [x] 1단계 환경, 라이브러리, 하드웨어 매핑
- [x] 2단계 폴더 구조, 모듈 인터페이스 헤더
- [x] 3단계 드라이버 + LVGL 바인딩 (진단 화면)
- [x] 4단계 UI (위젯형 / 슬라이드형, 데모 데이터)
- [ ] 5단계 서비스
  - [x] 5-1 NVS 설정, 설정 페이지, 화면 회전
  - [x] 5-2 Wi-Fi 연결/스캔, NTP(KST)
  - [x] 5-3 날씨(Open-Meteo), 주가(Yahoo)
  - [x] 5-4 전자앨범 (SD 하위 폴더, JPEG/PNG, EXIF 방향)
  - [x] 5-5 BLE 음악 리모컨 (iOS AMS / Android 미디어 키) - 코드만 유지, **기본 비활성** (내부 RAM 부족)
  - [x] 5-6 한글 폰트, 언어 설정(한국어/English), 날씨 아이콘
  - [x] 5-7 웹 UI 설정 (`http://smart-display.local`)
  - [x] 5-8 웹 UI 사진 업로드 (여러 장) + 자르기
  - [x] 5-9 Flickr 피드(RSS 2.0) 사진 동기화 → SD 카드 → 전자앨범 (`app_flickr` 이식)
- [ ] 6단계 GitHub Release OTA
