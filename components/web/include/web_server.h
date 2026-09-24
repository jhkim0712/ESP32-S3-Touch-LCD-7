// 같은 네트워크의 브라우저에서 설정을 바꾸는 웹 서버 (http://smart-display.local, mDNS).
//
// 페이지(www/index.html)는 빌드할 때 gzip 으로 압축해 펌웨어에 넣는다
// (OTA 로 앱을 바꾸면 웹 페이지도 같은 버전으로 함께 바뀐다).
//
// REST API (JSON). 모든 /api 요청은 "X-PIN" 헤더(또는 ?pin=)에 기기 설정 화면의 PIN 이 필요하다.
// PIN 이 5번 틀리면 30초 동안 모든 요청을 거부한다 (429).
//   GET  /api/status      펌웨어, Wi-Fi, 메모리, 시각, 날씨 상태
//   GET  /api/settings    현재 설정 (Wi-Fi 비밀번호는 보내지 않고 설정 여부만)
//   POST /api/settings    일부 항목만 보내도 됨 → NVS 저장 + APP_EVT_SETTINGS_CHANGED
//   POST /api/wifi/scan   주변 Wi-Fi 검색 (최대 10초 대기)
//   POST /api/restart     재부팅
//   /api/photos/...       전자앨범 사진 목록/업로드/삭제 (web_photos.c)
//   /api/feeds[/sync]     사진 피드(RSS) 목록/상태/동기화 (web_feeds.c)
//   /api/ota[/check|/install]  펌웨어 업데이트 확인/설치 (web_ota.c)
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WEB_HOSTNAME "smart-display"   // wifi_mgr 의 DHCP 호스트 이름과 같게 둔다

esp_err_t web_server_start(void);   // wifi_mgr_start() 이후 호출

#ifdef __cplusplus
}
#endif
