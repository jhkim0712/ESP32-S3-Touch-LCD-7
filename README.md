# ESP32-S3-Touch-LCD-7 Smart Display

[Waveshare ESP32-S3-Touch-LCD-7](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-7)(7인치 800x480 RGB LCD, GT911 터치, 8MB PSRAM)용 스마트 디스플레이 펌웨어입니다.

## 기능
| 기능 | 내용 |
|---|---|
| 시계 | Wi-Fi 연결 후 NTP로 KST(UTC+9) 동기화, 아날로그/디지털 시계 |
| 전자앨범 | SD 카드의 JPG/PNG 슬라이드쇼 (PSRAM에서 미리 디코딩) |
| 날씨 | Open-Meteo API로 현재 기온, 습도, 날씨 아이콘 표시 |
| 주가 | Yahoo Finance로 AAPL, NVDA, KOSPI(`^KS11`) 등 시세와 등락률 표시 |
| 음악 리모컨 | BLE로 스마트폰 음악 제어 (iOS: AMS로 곡 정보 표시 + 제어 / Android: 미디어 키 제어) |
| OTA | GitHub Releases에서 새 버전을 확인하고, 화면 팝업으로 승인하면 HTTPS로 업데이트 |
| UI | 위젯형(카드 그리드)과 슬라이드형(좌우 스와이프) 두 모드 전환 |

## 개발 환경
- ESP-IDF **v5.4 이상** (v5.5.1에서 확인 중)
- LVGL 9.2 + esp_lvgl_port
- 대상 칩: ESP32-S3 (8MB Flash / 8MB Octal PSRAM)

## 빌드 및 플래시
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

## 문서
- [1단계 — 개발 환경 및 라이브러리](docs/01-environment.md)
- [2단계 — 폴더 구조 및 아키텍처](docs/02-architecture.md)

## 진행 상황
- [x] 1단계 환경, 라이브러리, 하드웨어 매핑
- [x] 2단계 폴더 구조, 모듈 인터페이스 헤더
- [ ] 3단계 드라이버 + LVGL 바인딩
- [ ] 4단계 UI (위젯형 / 슬라이드형)
- [ ] 5단계 FreeRTOS 서비스 태스크
- [ ] 6단계 GitHub Release OTA
