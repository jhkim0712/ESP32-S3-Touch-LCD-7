// HTTPS GET → PSRAM 버퍼. 서버 인증서는 ESP-IDF 인증서 번들(esp_crt_bundle)로 검증한다.
// 응답 크기를 미리 모르는 경우(chunked)도 있어 버퍼를 늘려 가며 받는다.
// net_worker 한 태스크에서만 호출해 동시에 열리는 TLS 세션을 1개로 유지한다.

#include "net.h"

#include <stdlib.h>
#include <string.h>
#include "esp_check.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"

#define HTTP_TIMEOUT_MS     15000
#define HTTP_MAX_BODY       (512 * 1024)
#define HTTP_CHUNK          2048
#define HTTP_USER_AGENT     "Mozilla/5.0 (ESP32-S3; SmartDisplay)"

static const char *TAG = "http";

static esp_err_t grow(char **buf, size_t *cap, size_t need)
{
    if (need <= *cap) {
        return ESP_OK;
    }
    size_t new_cap = *cap ? *cap : 4096;
    while (new_cap < need) {
        new_cap *= 2;
    }
    char *p = heap_caps_realloc(*buf, new_cap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ESP_RETURN_ON_FALSE(p, ESP_ERR_NO_MEM, TAG, "no memory for %u bytes", (unsigned)new_cap);
    *buf = p;
    *cap = new_cap;
    return ESP_OK;
}

esp_err_t http_get_alloc(const char *url, const char *extra_headers,
                         char **out_body, size_t *out_len, int *out_status)
{
    *out_body = NULL;
    *out_len = 0;
    if (out_status) {
        *out_status = 0;
    }

    const esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = HTTP_TIMEOUT_MS,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = HTTP_USER_AGENT,
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .max_redirection_count = 5,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    ESP_RETURN_ON_FALSE(client, ESP_FAIL, TAG, "client init");

    // extra_headers: "Name: value\r\nName2: value2" (선택)
    if (extra_headers) {
        char *copy = strdup(extra_headers);
        char *save = NULL;
        for (char *line = strtok_r(copy, "\r\n", &save); line; line = strtok_r(NULL, "\r\n", &save)) {
            char *colon = strchr(line, ':');
            if (colon) {
                *colon = '\0';
                char *value = colon + 1;
                while (*value == ' ') {
                    value++;
                }
                esp_http_client_set_header(client, line, value);
            }
        }
        free(copy);
    }

    char *buf = NULL;
    size_t cap = 0, len = 0;
    esp_err_t err = ESP_OK;
    int status = 0;

    // 리다이렉트는 open → fetch_headers 단계에서 직접 따라가야 한다
    for (int hops = 0; hops <= cfg.max_redirection_count; hops++) {
        err = esp_http_client_open(client, 0);
        if (err != ESP_OK) {
            break;
        }
        int64_t content_len = esp_http_client_fetch_headers(client);
        status = esp_http_client_get_status_code(client);
        if (status >= 300 && status < 400) {
            esp_http_client_set_redirection(client);
            esp_http_client_close(client);
            continue;
        }
        if (content_len > HTTP_MAX_BODY) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        if (content_len > 0) {
            err = grow(&buf, &cap, (size_t)content_len + 1);
        }
        while (err == ESP_OK) {
            err = grow(&buf, &cap, len + HTTP_CHUNK + 1);
            if (err != ESP_OK) {
                break;
            }
            int n = esp_http_client_read(client, buf + len, HTTP_CHUNK);
            if (n < 0) {
                err = ESP_FAIL;
                break;
            }
            if (n == 0) {
                break;   // 끝
            }
            len += n;
            if (len > HTTP_MAX_BODY) {
                err = ESP_ERR_INVALID_SIZE;
            }
        }
        break;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (out_status) {
        *out_status = status;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GET %s failed: %s (HTTP %d)", url, esp_err_to_name(err), status);
        free(buf);
        return err;
    }
    if (!buf) {
        buf = heap_caps_malloc(1, MALLOC_CAP_SPIRAM);
        ESP_RETURN_ON_FALSE(buf, ESP_ERR_NO_MEM, TAG, "no memory");
    }
    buf[len] = '\0';
    *out_body = buf;
    *out_len = len;
    return ESP_OK;
}
