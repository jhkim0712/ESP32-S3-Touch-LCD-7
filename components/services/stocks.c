// 주가 조회
//   Yahoo Finance v8 chart API (비공식, 키 불필요): 종목당 요청 1회
//     meta.regularMarketPrice / meta.chartPreviousClose → 등락률 계산
//   Alpha Vantage GLOBAL_QUOTE (무료 25회/일): menuconfig 에서 선택 시
// 종목 하나가 실패하면 직전 값을 유지해 화면에서 줄이 사라지지 않게 한다.

#include "services.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"
#include "esp_check.h"
#include "esp_log.h"
#include "net.h"
#include "sdkconfig.h"

static const char *TAG = "stocks";

// 지수는 Yahoo 이름이 길어서 짧은 이름으로 표시
static const struct { const char *symbol, *name; } k_aliases[] = {
    { "^KS11", "KOSPI" }, { "^KQ11", "KOSDAQ" }, { "^GSPC", "S&P 500" },
    { "^IXIC", "NASDAQ" }, { "^DJI", "Dow Jones" }, { "^N225", "Nikkei 225" },
};

static const char *alias_for(const char *symbol)
{
    for (size_t i = 0; i < sizeof(k_aliases) / sizeof(k_aliases[0]); i++) {
        if (strcmp(k_aliases[i].symbol, symbol) == 0) {
            return k_aliases[i].name;
        }
    }
    return NULL;
}

static void url_encode(const char *in, char *out, size_t size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t o = 0;
    for (; *in && o + 4 < size; in++) {
        unsigned char c = (unsigned char)*in;
        if (isalnum(c) || c == '.' || c == '-' || c == '_' || c == '=') {
            out[o++] = (char)c;
        } else {
            out[o++] = '%';
            out[o++] = hex[c >> 4];
            out[o++] = hex[c & 15];
        }
    }
    out[o] = '\0';
}

static esp_err_t get_json(const char *url, cJSON **out)
{
    char *body = NULL;
    size_t len = 0;
    int status = 0;
    ESP_RETURN_ON_ERROR(http_get_alloc(url, NULL, &body, &len, &status), TAG, "request");
    if (status != 200) {
        ESP_LOGW(TAG, "HTTP %d for %s", status, url);
        free(body);
        return ESP_FAIL;
    }
    *out = cJSON_Parse(body);
    free(body);
    return *out ? ESP_OK : ESP_FAIL;
}

#if CONFIG_APP_STOCK_PROVIDER_ALPHAVANTAGE

static esp_err_t fetch_one(const char *symbol, stock_quote_t *q)
{
    char enc[48], url[256];
    url_encode(symbol, enc, sizeof(enc));
    snprintf(url, sizeof(url), "https://www.alphavantage.co/query?function=GLOBAL_QUOTE&symbol=%s&apikey=%s",
             enc, CONFIG_APP_STOCK_API_KEY);
    cJSON *root = NULL;
    ESP_RETURN_ON_ERROR(get_json(url, &root), TAG, "%s", symbol);

    esp_err_t err = ESP_FAIL;
    cJSON *gq = cJSON_GetObjectItem(root, "Global Quote");
    cJSON *price = cJSON_GetObjectItem(gq, "05. price");
    cJSON *pct = cJSON_GetObjectItem(gq, "10. change percent");
    if (cJSON_IsString(price) && cJSON_IsString(pct)) {
        q->price = strtod(price->valuestring, NULL);
        q->change_pct = strtod(pct->valuestring, NULL);   // "1.2345%"
        strlcpy(q->currency, "USD", sizeof(q->currency));
        err = ESP_OK;
    } else {
        ESP_LOGW(TAG, "%s: no quote (rate limit?)", symbol);
    }
    cJSON_Delete(root);
    return err;
}

#else   // Yahoo

static esp_err_t fetch_one(const char *symbol, stock_quote_t *q)
{
    char enc[48], url[256];
    url_encode(symbol, enc, sizeof(enc));
    snprintf(url, sizeof(url), "https://query1.finance.yahoo.com/v8/finance/chart/%s?range=1d&interval=1d", enc);
    cJSON *root = NULL;
    ESP_RETURN_ON_ERROR(get_json(url, &root), TAG, "%s", symbol);

    esp_err_t err = ESP_FAIL;
    cJSON *result = cJSON_GetArrayItem(cJSON_GetObjectItem(cJSON_GetObjectItem(root, "chart"), "result"), 0);
    cJSON *meta = cJSON_GetObjectItem(result, "meta");
    cJSON *price = cJSON_GetObjectItem(meta, "regularMarketPrice");
    cJSON *prev = cJSON_GetObjectItem(meta, "chartPreviousClose");
    if (!cJSON_IsNumber(prev)) {
        prev = cJSON_GetObjectItem(meta, "previousClose");
    }
    if (cJSON_IsNumber(price)) {
        q->price = price->valuedouble;
        double p = cJSON_IsNumber(prev) ? prev->valuedouble : 0;
        q->change_pct = p > 0 ? (q->price - p) / p * 100.0 : 0;

        cJSON *cur = cJSON_GetObjectItem(meta, "currency");
        strlcpy(q->currency, cJSON_IsString(cur) ? cur->valuestring : "", sizeof(q->currency));
        cJSON *name = cJSON_GetObjectItem(meta, "shortName");
        if (!cJSON_IsString(name)) {
            name = cJSON_GetObjectItem(meta, "longName");
        }
        const char *alias = alias_for(symbol);
        strlcpy(q->name, alias ? alias : (cJSON_IsString(name) ? name->valuestring : ""), sizeof(q->name));
        err = ESP_OK;
    } else {
        ESP_LOGW(TAG, "%s: no price in response", symbol);
    }
    cJSON_Delete(root);
    return err;
}

#endif

esp_err_t stocks_fetch(const char *symbols_csv, stock_list_t *out)
{
    stock_list_t *prev = calloc(1, sizeof(stock_list_t));
    ESP_RETURN_ON_FALSE(prev, ESP_ERR_NO_MEM, TAG, "no memory");
    app_state_get_stocks(prev);

    memset(out, 0, sizeof(*out));
    char *list = strdup(symbols_csv);
    int ok = 0;
    char *save = NULL;
    for (char *tok = strtok_r(list, ",", &save); tok && out->count < APP_MAX_STOCKS;
         tok = strtok_r(NULL, ",", &save)) {
        while (isspace((unsigned char)*tok)) {
            tok++;
        }
        char *end = tok + strlen(tok);
        while (end > tok && isspace((unsigned char)end[-1])) {
            *--end = '\0';
        }
        if (!*tok) {
            continue;
        }

        stock_quote_t *q = &out->items[out->count];
        memset(q, 0, sizeof(*q));
        strlcpy(q->symbol, tok, sizeof(q->symbol));
        if (fetch_one(tok, q) == ESP_OK) {
            ok++;
            out->count++;
            continue;
        }
        // 실패: 직전 값 유지
        for (int i = 0; i < prev->count; i++) {
            if (strcmp(prev->items[i].symbol, tok) == 0) {
                *q = prev->items[i];
                out->count++;
                break;
            }
        }
    }
    free(list);
    free(prev);

    out->updated_at = time(NULL);
    ESP_LOGI(TAG, "%d/%d quotes updated", ok, out->count);
    return ok > 0 ? ESP_OK : ESP_FAIL;
}
