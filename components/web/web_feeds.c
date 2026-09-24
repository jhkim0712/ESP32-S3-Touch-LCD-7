// 사진 피드(RSS 2.0 / Atom) API (photo_feed)
//   GET  /api/feeds         {feeds:[url...], status:{syncing, last_sync, image_count, unsupported, skipped, error}}
//   POST /api/feeds         {feeds:[url...]} → NVS 저장 + 바로 동기화 (삭제한 피드의 사진도 지워짐)
//   POST /api/feeds/sync    지금 동기화
// 동기화 자체는 net_worker 가 한다 (APP_EVT_REQ_FEED_SYNC).

#include "web_internal.h"

#include <stdlib.h>
#include <string.h>
#include "photo_feed.h"
#include "app_state.h"
#include "app_storage.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "web_feeds";

static bool valid_feed_url(const char *url)
{
    size_t len = strlen(url);
    if (len >= SETTINGS_FEED_URL_MAX || (strncmp(url, "https://", 8) != 0 && strncmp(url, "http://", 7) != 0)) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)url; *p; p++) {
        if (*p <= 0x20 || *p >= 0x7F) {
            return false;   // 공백, 줄바꿈(목록 구분자), 비 ASCII: 브라우저가 인코딩해서 보내야 함
        }
    }
    return true;
}

// 줄바꿈으로 구분한 목록에 line 과 똑같은 줄이 있는지
static bool has_line(const char *list, const char *line)
{
    size_t len = strlen(line);
    for (const char *p = list; *p;) {
        const char *end = strchr(p, '\n');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n == len && strncmp(p, line, len) == 0) {
            return true;
        }
        p += n + (end ? 1 : 0);
    }
    return false;
}

static esp_err_t get_feeds(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return ESP_OK;
    }
    char *feeds = heap_caps_malloc(SETTINGS_FEED_FEEDS_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!feeds) {
        return web_send_error(req, "500 Internal Server Error", "no memory");
    }
    settings_get_photo_feeds(feeds, SETTINGS_FEED_FEEDS_LEN);

    cJSON *root = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(root, "feeds");
    char *save = NULL;
    for (char *url = strtok_r(feeds, "\n", &save); url; url = strtok_r(NULL, "\n", &save)) {
        cJSON_AddItemToArray(list, cJSON_CreateString(url));
    }
    free(feeds);

    photo_feed_status_t st;
    photo_feed_get_status(&st);
    cJSON *status = cJSON_AddObjectToObject(root, "status");
    cJSON_AddBoolToObject(status, "syncing", st.syncing);
    cJSON_AddNumberToObject(status, "last_sync", (double)st.last_sync);
    cJSON_AddNumberToObject(status, "image_count", st.image_count);
    cJSON_AddNumberToObject(status, "unsupported", st.unsupported);
    cJSON_AddNumberToObject(status, "skipped", st.skipped);
    cJSON_AddStringToObject(status, "error", st.have_error ? st.error : "");
    cJSON_AddNumberToObject(root, "max_feeds", SETTINGS_FEED_MAX_FEEDS);
    return web_send_json(req, root);
}

static esp_err_t post_feeds(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return ESP_OK;
    }
    cJSON *root = web_read_json_body(req);
    if (!root) {
        return ESP_OK;
    }
    const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "feeds");
    if (!cJSON_IsArray(list) || cJSON_GetArraySize(list) > SETTINGS_FEED_MAX_FEEDS) {
        cJSON_Delete(root);
        return web_send_error(req, "400 Bad Request", "feeds");
    }
    char *feeds = heap_caps_calloc(1, SETTINGS_FEED_FEEDS_LEN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!feeds) {
        cJSON_Delete(root);
        return web_send_error(req, "500 Internal Server Error", "no memory");
    }
    const char *bad = NULL;
    const cJSON *item;
    cJSON_ArrayForEach(item, list) {
        const char *url = cJSON_GetStringValue(item);
        if (!url || !valid_feed_url(url)) {
            bad = "url";
            break;
        }
        if (has_line(feeds, url)) {
            continue;   // 같은 URL 중복
        }
        if (feeds[0]) {
            strlcat(feeds, "\n", SETTINGS_FEED_FEEDS_LEN);
        }
        strlcat(feeds, url, SETTINGS_FEED_FEEDS_LEN);
    }
    cJSON_Delete(root);

    esp_err_t err = bad ? ESP_ERR_INVALID_ARG : settings_set_photo_feeds(feeds);
    free(feeds);
    if (bad) {
        return web_send_error(req, "400 Bad Request", bad);
    }
    if (err != ESP_OK) {
        return web_send_error(req, "500 Internal Server Error", "save failed");
    }
    ESP_LOGI(TAG, "feed list saved");
    app_event_post(APP_EVT_REQ_FEED_SYNC, NULL, 0);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t post_sync(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return ESP_OK;
    }
    app_event_post(APP_EVT_REQ_FEED_SYNC, NULL, 0);
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t web_feeds_register(httpd_handle_t server)
{
    static const httpd_uri_t uris[] = {
        { .uri = "/api/feeds",      .method = HTTP_GET,  .handler = get_feeds },
        { .uri = "/api/feeds",      .method = HTTP_POST, .handler = post_feeds },
        { .uri = "/api/feeds/sync", .method = HTTP_POST, .handler = post_sync },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &uris[i]), TAG, "uri %s", uris[i].uri);
    }
    return ESP_OK;
}
