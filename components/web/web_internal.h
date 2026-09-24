// web 컴포넌트 내부 공유 선언. 모든 함수는 HTTP 서버 태스크(핸들러 안)에서만 호출한다.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"
#include "esp_http_server.h"

// JSON 오류 응답 {"error": message}. status 예: "400 Bad Request"
esp_err_t web_send_error(httpd_req_t *req, const char *status, const char *message);
// root 를 보내고 해제한다 (NULL 이면 500)
esp_err_t web_send_json(httpd_req_t *req, cJSON *root);
// PIN 확인 (X-PIN 헤더, 없으면 ?pin=). 실패하면 오류 응답까지 보내고 false.
bool web_authorized(httpd_req_t *req);
// 요청 본문(JSON 객체, 최대 4KB)을 읽는다. 실패하면 오류 응답까지 보내고 NULL.
cJSON *web_read_json_body(httpd_req_t *req);
// URL 쿼리 값을 URL 디코딩해 out 에 복사한다. 없거나 너무 길면 false (out 은 "").
bool web_query_param(httpd_req_t *req, const char *key, char *out, size_t size);

// 사진 API (web_photos.c)
esp_err_t web_photos_register(httpd_handle_t server);
// Flickr 피드 API (web_flickr.c)
esp_err_t web_flickr_register(httpd_handle_t server);
