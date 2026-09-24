// 펌웨어 업데이트 API (ota)
//   GET  /api/ota           {current, state, progress, error, last_check, available, latest:{tag, notes, size}}
//   POST /api/ota/check     지금 확인 (결과는 GET 으로 다시 읽음)
//   POST /api/ota/install   확인된 새 버전 설치 → 끝나면 기기가 재부팅
// 확인과 설치는 net_worker 가 한다 (APP_EVT_REQ_OTA_CHECK / APP_EVT_REQ_OTA_START).

#include "web_internal.h"

#include <stdlib.h>
#include "app_state.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "ota.h"

static const char *TAG = "web_ota";

static const char *state_name(ota_state_t state)
{
    switch (state) {
    case OTA_STATE_CHECKING:    return "checking";
    case OTA_STATE_DOWNLOADING: return "downloading";
    case OTA_STATE_DONE:        return "done";
    case OTA_STATE_FAILED:      return "failed";
    default:                    return "idle";
    }
}

static esp_err_t get_ota(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return ESP_OK;
    }
    ota_status_t *st = malloc(sizeof(ota_status_t));
    if (!st) {
        return web_send_error(req, "500 Internal Server Error", "no memory");
    }
    ota_get_status(st);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "current", esp_app_get_description()->version);
    cJSON_AddStringToObject(root, "state", state_name(st->state));
    cJSON_AddNumberToObject(root, "progress", st->progress);
    cJSON_AddStringToObject(root, "error", st->error);
    cJSON_AddNumberToObject(root, "last_check", (double)st->last_check);
    cJSON_AddBoolToObject(root, "available", st->has_latest && st->available);
    if (st->has_latest) {
        cJSON *latest = cJSON_AddObjectToObject(root, "latest");
        cJSON_AddStringToObject(latest, "tag", st->latest.tag);
        cJSON_AddStringToObject(latest, "notes", st->latest.notes);
        cJSON_AddNumberToObject(latest, "size", st->latest.asset_size);
    }
    free(st);
    return web_send_json(req, root);
}

static esp_err_t post_check(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return ESP_OK;
    }
    app_event_post(APP_EVT_REQ_OTA_CHECK, NULL, 0);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t post_install(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return ESP_OK;
    }
    ota_status_t *st = malloc(sizeof(ota_status_t));
    if (!st) {
        return web_send_error(req, "500 Internal Server Error", "no memory");
    }
    ota_get_status(st);
    bool available = st->has_latest && st->available;
    bool busy = st->state == OTA_STATE_DOWNLOADING || st->state == OTA_STATE_DONE;
    free(st);
    if (!available) {
        return web_send_error(req, "409 Conflict", "no update");
    }
    if (!busy) {
        app_event_post(APP_EVT_REQ_OTA_START, NULL, 0);
    }
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t web_ota_register(httpd_handle_t server)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/api/ota",         .method = HTTP_GET,  .handler = get_ota },
        { .uri = "/api/ota/check",   .method = HTTP_POST, .handler = post_check },
        { .uri = "/api/ota/install", .method = HTTP_POST, .handler = post_install },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &uris[i]), TAG, "uri %s", uris[i].uri);
    }
    return ESP_OK;
}
