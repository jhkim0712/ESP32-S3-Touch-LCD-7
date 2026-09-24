// GitHub Releases OTA (ota.h 참고). ota_check / ota_install 은 net_worker 태스크에서만 호출된다.
// 상태(s_status)는 웹 서버 태스크도 읽으므로 mutex 로 보호한다.

#include "ota.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_state.h"
#include "board.h"
#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "net.h"
#include "sdkconfig.h"

#define API_URL             "https://api.github.com/repos/" CONFIG_APP_OTA_GITHUB_OWNER "/" \
                            CONFIG_APP_OTA_GITHUB_REPO "/releases/latest"
#define API_HEADERS         "Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28"
#define USER_AGENT          "Mozilla/5.0 (ESP32-S3; SmartDisplay)"
#define VALID_AFTER_US      (60 * 1000 * 1000LL)   // 새 펌웨어가 이만큼 동작하면 롤백 취소
#define RESTART_DELAY_US    (3 * 1000 * 1000LL)    // 완료 메시지를 보여 줄 시간

static const char *TAG = "ota";

static SemaphoreHandle_t  s_lock;
static ota_status_t       s_status;
static char               s_notified_tag[32];      // 이 태그는 이미 팝업을 띄웠음
static esp_timer_handle_t s_valid_timer;
static esp_timer_handle_t s_restart_timer;

// ---- 상태 ----

static void set_state(ota_state_t state, int progress, const char *error)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = state;
    s_status.progress = progress;
    if (error) {
        strlcpy(s_status.error, error, sizeof(s_status.error));
    }
    xSemaphoreGive(s_lock);
}

void ota_get_status(ota_status_t *out)
{
    if (!s_lock) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_lock);
}

// ---- 버전 비교 ----

// "v1.2.3" / "1.2.3-rc1" → 숫자 3개. 뒤에 붙은 "-..." (사전 배포)는 같은 번호의 정식 버전보다 낮다.
static void parse_version(const char *s, int v[3], bool *prerelease)
{
    if (*s == 'v' || *s == 'V') {
        s++;
    }
    v[0] = v[1] = v[2] = 0;
    char *end = (char *)s;
    for (int i = 0; i < 3; i++) {
        v[i] = (int)strtol(s, &end, 10);
        if (*end != '.') {
            break;
        }
        s = end + 1;
    }
    *prerelease = *end == '-';
}

static bool is_newer(const char *tag, const char *current)
{
    int a[3], b[3];
    bool a_pre, b_pre;
    parse_version(tag, a, &a_pre);
    parse_version(current, b, &b_pre);
    for (int i = 0; i < 3; i++) {
        if (a[i] != b[i]) {
            return a[i] > b[i];
        }
    }
    return b_pre && !a_pre;   // 1.0.0 > 1.0.0-rc1
}

// 한글 등 여러 바이트 글자가 중간에서 잘리지 않게 끝을 정리한다
static void utf8_trim_tail(char *s)
{
    size_t len = strlen(s);
    size_t i = len;
    while (i > 0 && ((unsigned char)s[i - 1] & 0xC0) == 0x80) {
        i--;   // 이어지는 바이트
    }
    if (i > 0) {
        unsigned char lead = (unsigned char)s[i - 1];
        size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        if (len - (i - 1) < need) {
            s[i - 1] = '\0';   // 마지막 글자가 불완전
        }
    }
}

// ---- 확인 ----

static esp_err_t parse_release(const char *json, ota_release_t *out, const char **error)
{
    cJSON *root = cJSON_Parse(json);
    const char *tag = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "tag_name"));
    if (!tag) {
        cJSON_Delete(root);
        *error = "invalid response";
        return ESP_FAIL;
    }
    memset(out, 0, sizeof(*out));
    strlcpy(out->tag, tag, sizeof(out->tag));
    const char *body = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "body"));
    if (body) {
        strlcpy(out->notes, body, sizeof(out->notes));
        utf8_trim_tail(out->notes);
    }
    const cJSON *asset;
    cJSON_ArrayForEach(asset, cJSON_GetObjectItemCaseSensitive(root, "assets")) {
        const char *name = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(asset, "name"));
        const char *url = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(asset, "browser_download_url"));
        if (name && url && strcmp(name, CONFIG_APP_OTA_ASSET_NAME) == 0 && strlen(url) < sizeof(out->asset_url)) {
            strlcpy(out->asset_url, url, sizeof(out->asset_url));
            out->asset_size = (int)cJSON_GetNumberValue(cJSON_GetObjectItemCaseSensitive(asset, "size"));
            break;
        }
    }
    cJSON_Delete(root);
    if (!out->asset_url[0]) {
        *error = "no " CONFIG_APP_OTA_ASSET_NAME " in release";
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

esp_err_t ota_check(void)
{
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    set_state(OTA_STATE_CHECKING, 0, NULL);

    char *body = NULL;
    size_t len = 0;
    int status = 0;
    const char *error = NULL;
    ota_release_t *rel = calloc(1, sizeof(ota_release_t));
    esp_err_t err = rel ? http_get_alloc(API_URL, API_HEADERS, &body, &len, &status) : ESP_ERR_NO_MEM;
    if (err != ESP_OK) {
        error = rel ? "connection failed" : "no memory";
    } else if (status == 404) {
        err = ESP_ERR_NOT_FOUND;
        error = "no release published";
    } else if (status == 403 || status == 429) {
        err = ESP_FAIL;
        error = "GitHub rate limit";
    } else if (status != 200) {
        err = ESP_FAIL;
        error = "GitHub API error";
    } else {
        err = parse_release(body, rel, &error);
    }
    free(body);

    const char *current = esp_app_get_description()->version;
    bool newer = err == ESP_OK && is_newer(rel->tag, current);
    bool notify = newer && strcmp(s_notified_tag, rel->tag) != 0;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.state = err == ESP_OK ? OTA_STATE_IDLE : OTA_STATE_FAILED;
    s_status.last_check = time(NULL);
    if (err == ESP_OK) {
        s_status.has_latest = true;
        s_status.available = newer;
        s_status.latest = *rel;
        s_status.error[0] = '\0';
    } else {
        strlcpy(s_status.error, error, sizeof(s_status.error));
    }
    xSemaphoreGive(s_lock);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "latest release %s, running %s%s", rel->tag, current, newer ? " -> update available" : "");
    } else {
        ESP_LOGW(TAG, "check failed: %s (HTTP %d)", error, status);
    }
    if (notify) {
        strlcpy(s_notified_tag, rel->tag, sizeof(s_notified_tag));
        app_event_post(APP_EVT_OTA_AVAILABLE, rel, sizeof(*rel));   // UI 팝업
    }
    free(rel);
    return err;
}

// ---- 설치 ----

static void on_restart_timer(void *arg)
{
    board_backlight_set(false);   // 재부팅 중 노이즈 화면을 감춘다
    esp_restart();
}

static void report_progress(int percent)
{
    set_state(OTA_STATE_DOWNLOADING, percent, NULL);
    app_event_post(APP_EVT_OTA_PROGRESS, &percent, sizeof(percent));
}

static esp_err_t finish_install(esp_err_t err, const char *error)
{
    set_state(err == ESP_OK ? OTA_STATE_DONE : OTA_STATE_FAILED, err == ESP_OK ? 100 : 0, err == ESP_OK ? "" : error);
    app_event_post(APP_EVT_OTA_DONE, &err, sizeof(err));
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "update installed, restarting");
        esp_timer_start_once(s_restart_timer, RESTART_DELAY_US);
    } else {
        ESP_LOGE(TAG, "update failed: %s (%s)", error, esp_err_to_name(err));
    }
    return err;
}

esp_err_t ota_install(void)
{
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    ota_status_t *st = malloc(sizeof(ota_status_t));
    if (!st) {
        return finish_install(ESP_ERR_NO_MEM, "no memory");
    }
    ota_get_status(st);
    if (st->state == OTA_STATE_DONE) {
        free(st);
        return ESP_OK;   // 이미 설치됨, 재부팅 대기 중
    }
    if (!st->has_latest || !st->available) {
        free(st);
        return finish_install(ESP_ERR_INVALID_STATE, "no update available");
    }
    if (!wifi_mgr_is_connected()) {
        free(st);
        return finish_install(ESP_ERR_INVALID_STATE, "no network");
    }
    ESP_LOGI(TAG, "installing %s from %s", st->latest.tag, st->latest.asset_url);
    report_progress(0);

    const esp_http_client_config_t http = {
        .url = st->latest.asset_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
        .user_agent = USER_AGENT,
        .keep_alive_enable = true,
        .max_redirection_count = 5,
        // GitHub 은 서명된 긴 URL(수백 자)로 리다이렉트한다: 요청 줄/헤더 버퍼를 넉넉히
        .buffer_size = 4096,
        .buffer_size_tx = 2048,
    };
    const esp_https_ota_config_t cfg = { .http_config = &http };
    int asset_size = st->latest.asset_size;

    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&cfg, &handle);
    free(st);   // http.url 이 가리키므로 begin(HTTP 클라이언트가 URL 을 복사) 이후에 해제
    if (err != ESP_OK) {
        return finish_install(err, "download failed");
    }

    // 다른 프로젝트의 파일을 잘못 올린 경우를 막는다 (칩 종류는 esp_https_ota 가 확인)
    esp_app_desc_t desc;
    err = esp_https_ota_get_img_desc(handle, &desc);
    if (err == ESP_OK && strcmp(desc.project_name, esp_app_get_description()->project_name) != 0) {
        ESP_LOGE(TAG, "image is \"%s\", expected \"%s\"", desc.project_name, esp_app_get_description()->project_name);
        esp_https_ota_abort(handle);
        return finish_install(ESP_ERR_INVALID_VERSION, "wrong firmware image");
    }

    int last = 0;
    while (err == ESP_OK) {
        err = esp_https_ota_perform(handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS && err != ESP_OK) {
            break;
        }
        int size = esp_https_ota_get_image_size(handle);
        if (size <= 0) {
            size = asset_size;
        }
        int percent = size > 0 ? (int)((int64_t)esp_https_ota_get_image_len_read(handle) * 100 / size) : 0;
        if (percent > 99) {
            percent = 99;   // 100 은 검증까지 끝난 뒤
        }
        if (percent != last) {
            last = percent;
            report_progress(percent);
        }
        if (err == ESP_OK) {
            break;   // 모두 받음
        }
        err = ESP_OK;   // IN_PROGRESS → 계속
    }
    if (err == ESP_OK && !esp_https_ota_is_complete_data_received(handle)) {
        err = ESP_FAIL;
    }
    if (err != ESP_OK) {
        esp_https_ota_abort(handle);
        return finish_install(err, "download failed");
    }
    err = esp_https_ota_finish(handle);   // 이미지 검증 + 부트 파티션 변경
    return finish_install(err, err == ESP_ERR_OTA_VALIDATE_FAILED ? "invalid image" : "install failed");
}

// ---- 부팅 ----

static void on_valid_timer(void *arg)
{
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "new firmware marked valid: %s", esp_err_to_name(err));
}

esp_err_t ota_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(s_lock, ESP_ERR_NO_MEM, TAG, "mutex");

    const esp_timer_create_args_t restart_args = { .callback = on_restart_timer, .name = "ota_restart" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&restart_args, &s_restart_timer), TAG, "timer");

    // OTA 로 막 바뀐 펌웨어면 일정 시간 정상 동작한 뒤 유효로 표시한다.
    // 그 전에 재부팅되면(멈춤, watchdog, 전원) 부트로더가 이전 펌웨어로 되돌린다.
    esp_ota_img_states_t state;
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGW(TAG, "running new firmware from %s, marking valid in %d s", running->label,
                 (int)(VALID_AFTER_US / 1000000));
        const esp_timer_create_args_t valid_args = { .callback = on_valid_timer, .name = "ota_valid" };
        ESP_RETURN_ON_ERROR(esp_timer_create(&valid_args, &s_valid_timer), TAG, "timer");
        ESP_RETURN_ON_ERROR(esp_timer_start_once(s_valid_timer, VALID_AFTER_US), TAG, "timer start");
    }
    return ESP_OK;
}
