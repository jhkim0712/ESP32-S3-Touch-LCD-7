# ESP32-S3-Touch-LCD-7 Smart Display

[Waveshare ESP32-S3-Touch-LCD-7](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7)(7인치 800x480 RGB LCD, GT911 터치, 8MB PSRAM)용 스마트 디스플레이 펌웨어입니다.

## 기능
| 기능 | 내용 |
|---|---|
| 시계 | Wi-Fi 연결 후 NTP로 KST(UTC+9) 동기화, 아날로그/디지털 시계 |
| 전자앨범 | SD 카드의 JPG/PNG 슬라이드쇼 (PSRAM에서 미리 디코딩) |
| 날씨 | Open-Meteo API로 현재 기온, 습도, 날씨 아이콘 표시 |
| 주가 | Yahoo Finance로 AAPL, NVDA, KOSPI(`^KS11`) 등 시세와 등락률 표시 |
| 음악 리모컨 | BLE로 스마트폰 음악 제어 (iOS: AMS / Android: 미디어 키). 내부 RAM 부족으로 **기본 비활성**, menuconfig `CONFIG_APP_BLE_MEDIA` 로 켤 수 있음 |
| OTA | GitHub Releases에서 새 버전을 확인하고, 화면 팝업으로 승인하면 HTTPS로 업데이트 |
| UI | 위젯형(카드 그리드)과 슬라이드형(좌우 스와이프) 두 모드 전환 |

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
docs/            설계 문서
```

## 전자앨범 사진 준비
SD 카드 `photos/` 폴더(하위 폴더 4단계까지)에 JPG/PNG 를 넣습니다. 기기의 JPEG 디코더는
baseline JPEG 만 지원하고 PNG 는 약 600x400 이하만 풀 수 있으므로, PC 에서 변환해 넣는 것을 권장합니다.
```powershell
pip install pillow        # 아이폰 HEIC: pip install pillow-heif
python tools/prepare_photos.py  D:\MyPhotos  F:\photos
```

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
  - [ ] 5-6 한글 폰트, 언어 설정(한국어/English), 날씨 아이콘
  - [ ] 5-7 웹 UI 설정 (`http://smart-display.local`)
  - [ ] 5-8 웹 UI 사진 업로드 (여러 장) + 자르기
  - [ ] 5-9 RSS 이미지 피드 동기화 → SD 카드
- [ ] 6단계 GitHub Release OTA
