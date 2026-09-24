/**
 * @file app_flickr.c
 * @brief See app_flickr.h. The feed is fetched with net's http_get_alloc();
 *        images are streamed straight to the SD card with esp_http_client
 *        open/read (+ crt bundle) and manual redirect handling, which
 *        open/read - unlike esp_http_client_perform() - doesn't do on its own.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <inttypes.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

#include "app_flickr.h"
#include "app_state.h"
#include "app_storage.h"
#include "board.h"
#include "net.h"

static const char *TAG = "app_flickr";

#define PHOTOS_DIR        CONFIG_APP_PHOTO_DIR
#define FLICKR_DIR        APP_FLICKR_DIR

#define FEED_MAX_LEN      (256 * 1024)          /* Flickr's 20-item feeds are well under 100KB */
#define IMAGE_MAX_LEN     (4 * 1024 * 1024)     /* refuse anything absurd rather than fill the card */
#define MAX_ITEMS         50                    /* per feed - Flickr's own feeds return 20 */
#define URL_MAX           SETTINGS_FLICKR_URL_MAX
#define NAME_MAX_LEN      96
#define PATH_MAX_LEN      (sizeof(FLICKR_DIR) + 10 + NAME_MAX_LEN + 8)
#define MAX_REDIRECTS     5
#define IO_CHUNK          4096
#define MAX_ORPHAN_DIRS   16
#define USER_AGENT        "Mozilla/5.0 (ESP32-S3; SmartDisplay)"

static SemaphoreHandle_t s_lock;
static app_flickr_status_t s_status;

/* Scratch buffers, only ever touched from net_worker - kept off its stack
 * (and in PSRAM: net_worker's stack is internal RAM, needed for TLS). */
static char *s_feeds;
static char (*s_urls)[URL_MAX];
static char (*s_keep)[NAME_MAX_LEN];
static char (*s_doomed)[NAME_MAX_LEN];
static char (*s_orphans)[NAME_MAX_LEN];

/* ---------------------------------------------------------------------- */
/* Small helpers                                                           */
/* ---------------------------------------------------------------------- */

static bool has_extension(const char *name, const char *ext)
{
    size_t name_len = strlen(name);
    size_t ext_len = strlen(ext);
    return name_len >= ext_len && strcasecmp(name + (name_len - ext_len), ext) == 0;
}

/* Same set photo_loader plays - anything else in a feed is skipped. */
static bool is_image_name(const char *name)
{
    return has_extension(name, ".jpg") || has_extension(name, ".jpeg") || has_extension(name, ".png");
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

/* FNV-1a: gives every feed URL a short, stable folder name. */
static void feed_hash(const char *url, char out[9])
{
    uint32_t h = 2166136261u;
    for (const char *p = url; *p; p++) {
        h ^= (uint8_t)*p;
        h *= 16777619u;
    }
    snprintf(out, 9, "%08" PRIx32, h);
}

/* In-place decode of the handful of entities that can show up in a URL
 * attribute (really only &amp;, but the rest cost nothing). */
static void xml_unescape(char *s)
{
    static const struct { const char *ent; char ch; } ents[] = {
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}, {"&#38;", '&'},
    };
    char *w = s;
    for (char *r = s; *r;) {
        bool matched = false;
        if (*r == '&') {
            for (size_t ent_i = 0; ent_i < sizeof(ents) / sizeof(ents[0]); ent_i++) {
                size_t len = strlen(ents[ent_i].ent);
                if (strncmp(r, ents[ent_i].ent, len) == 0) {
                    *w++ = ents[ent_i].ch;
                    r += len;
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) {
            *w++ = *r++;
        }
    }
    *w = '\0';
}

/* Local filename for an image URL: its last path segment (query/fragment
 * dropped), with anything FAT or a path might choke on replaced by '_'.
 * @return false if the URL has no usable name or isn't a supported image. */
static bool url_to_name(const char *url, char *out, size_t out_len)
{
    const char *end = url + strcspn(url, "?#");
    const char *start = end;
    while (start > url && start[-1] != '/') {
        start--;
    }
    size_t len = (size_t)(end - start);
    if (len == 0 || len >= out_len) {
        return false;
    }
    for (size_t char_i = 0; char_i < len; char_i++) {
        char c = start[char_i];
        out[char_i] = (isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_') ? c : '_';
    }
    out[len] = '\0';
    return out[0] != '.' && is_image_name(out);
}

/* Flickr static image URLs end in "<id>_<secret>_<size>.jpg". The feed links
 * the 1024px "_b" size; "_c" is the same photo at 800px, which exactly covers
 * the 800x480 panel but is a fraction of the download and decode cost. The
 * larger sizes (_h/_k/_o) use a different secret, so they're left alone - and
 * the caller falls back to the original URL if the rewritten one turns out
 * not to exist anyway. */
static void prefer_800px(char *url)
{
    if (!strstr(url, "staticflickr.com/") || strpbrk(url, "?#")) {
        return;
    }
    char *dot = strrchr(url, '.');
    if (!dot || dot - url < 2 || dot[-2] != '_' || dot[-1] != 'b' || strcasecmp(dot, ".jpg") != 0) {
        return;
    }
    dot[-1] = 'c';
}

/* The photo frame's decoder (TJpgDec) only handles baseline JPEGs. Walks the
 * marker segments up to the first SOF: SOF2/6/10/14 are progressive. */
static bool jpeg_is_progressive(const char *path)
{
    if (!has_extension(path, ".jpg") && !has_extension(path, ".jpeg")) {
        return false;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    bool progressive = false;
    uint8_t hdr[4];
    if (fread(hdr, 1, 2, f) == 2 && hdr[0] == 0xFF && hdr[1] == 0xD8) {
        for (int seg = 0; seg < 64; seg++) {
            if (fread(hdr, 1, 4, f) != 4 || hdr[0] != 0xFF) {
                break;
            }
            uint8_t marker = hdr[1];
            if (marker == 0xC2 || marker == 0xC6 || marker == 0xCA || marker == 0xCE) {
                progressive = true;
                break;
            }
            if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
                break; /* any other SOF: baseline / extended */
            }
            uint16_t len = (uint16_t)((hdr[2] << 8) | hdr[3]);
            if (len < 2 || fseek(f, len - 2, SEEK_CUR) != 0) {
                break;
            }
        }
    }
    fclose(f);
    return progressive;
}

/* ---------------------------------------------------------------------- */
/* Feed parsing                                                            */
/* ---------------------------------------------------------------------- */

/* Copies attribute `attr`'s value (XML-unescaped) out of the tag whose
 * attributes span [attrs, attrs_end). */
static bool get_attr(const char *attrs, const char *attrs_end, const char *attr, char *out, size_t out_len)
{
    size_t attr_len = strlen(attr);
    for (const char *p = attrs; p + attr_len + 2 < attrs_end; p++) {
        if (!isspace((unsigned char)p[-1]) || strncmp(p, attr, attr_len) != 0 || p[attr_len] != '=') {
            continue;
        }
        char quote = p[attr_len + 1];
        if (quote != '"' && quote != '\'') {
            return false;
        }
        const char *value = p + attr_len + 2;
        const char *value_end = memchr(value, quote, (size_t)(attrs_end - value));
        if (!value_end || (size_t)(value_end - value) >= out_len) {
            return false;
        }
        memcpy(out, value, (size_t)(value_end - value));
        out[value_end - value] = '\0';
        xml_unescape(out);
        return true;
    }
    return false;
}

/* Looks through every <tag ...> in [block, block_end) for attribute `attr`,
 * optionally only in tags where attribute `filter_attr` equals `filter_val`
 * (for Atom's <link rel="enclosure" href="...">). */
static bool find_tag_attr(const char *block, const char *block_end, const char *tag, const char *attr,
                          const char *filter_attr, const char *filter_val, char *out, size_t out_len)
{
    size_t tag_len = strlen(tag);
    for (const char *p = block; (p = strstr(p, tag)) != NULL && p < block_end; p += tag_len) {
        char next = p[tag_len];
        if (!isspace((unsigned char)next)) {
            continue; /* a tag with no attributes, or just a longer tag name sharing this prefix */
        }
        const char *tag_end = strchr(p, '>');
        if (!tag_end || tag_end > block_end) {
            return false;
        }
        const char *attrs = p + tag_len;
        if (filter_attr) {
            char value[32];
            if (!get_attr(attrs, tag_end, filter_attr, value, sizeof(value)) || strcmp(value, filter_val) != 0) {
                continue;
            }
        }
        if (get_attr(attrs, tag_end, attr, out, out_len)) {
            return true;
        }
    }
    return false;
}

/* Extracts each item's image URL from an RSS 2.0 (<item>) or Atom (<entry>)
 * feed. Flickr puts it in <media:content url>, and also in <enclosure url>
 * (RSS) or <link rel="enclosure" href> (Atom) - any of the three will do,
 * which also covers non-Flickr photo feeds shaped the same way.
 * @return number of distinct image URLs written to urls[]. */
static size_t parse_feed(const char *doc, char (*urls)[URL_MAX], size_t max_urls)
{
    size_t count = 0;
    const char *p = doc;
    while (count < max_urls) {
        const char *item = strstr(p, "<item");
        const char *entry = strstr(p, "<entry");
        const char *start;
        const char *close_tag;
        if (item && (!entry || item < entry)) {
            start = item;
            close_tag = "</item>";
        } else if (entry) {
            start = entry;
            close_tag = "</entry>";
        } else {
            break;
        }
        const char *end = strstr(start, close_tag);
        if (!end) {
            break; /* truncated feed - stop at the last complete item */
        }

        char *url = urls[count];
        bool found = find_tag_attr(start, end, "<media:content", "url", NULL, NULL, url, URL_MAX) ||
                     find_tag_attr(start, end, "<enclosure", "url", NULL, NULL, url, URL_MAX) ||
                     find_tag_attr(start, end, "<link", "href", "rel", "enclosure", url, URL_MAX);
        char name[NAME_MAX_LEN];
        if (found && (strncmp(url, "https://", 8) == 0 || strncmp(url, "http://", 7) == 0) &&
            url_to_name(url, name, sizeof(name))) {
            bool dup = false;
            for (size_t url_i = 0; url_i < count && !dup; url_i++) {
                dup = strcmp(urls[url_i], url) == 0;
            }
            if (!dup) {
                count++;
            }
        }
        p = end + strlen(close_tag);
    }
    return count;
}

/* ---------------------------------------------------------------------- */
/* HTTP                                                                    */
/* ---------------------------------------------------------------------- */

/* Opens a GET request to `url`, following redirects, and returns the client
 * with the final response's headers already read (status in *out_status),
 * ready for esp_http_client_read(). The caller must close + clean it up.
 * @return NULL if the connection itself failed. */
static esp_http_client_handle_t http_open(const char *url, int *out_status)
{
    esp_http_client_config_t config = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        /* Flickr's CDN sends some long header lines (CSP, cookies) - the
         * 512-byte default buffers are too tight for them. */
        .buffer_size = 2048,
        .buffer_size_tx = 1024,
        .user_agent = USER_AGENT,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return NULL;
    }

    for (int redirects = 0;; redirects++) {
        if (esp_http_client_open(client, 0) != ESP_OK) {
            break;
        }
        esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        bool is_redirect = status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
        if (!is_redirect || redirects >= MAX_REDIRECTS) {
            *out_status = status;
            return client;
        }
        esp_http_client_flush_response(client, NULL);
        if (esp_http_client_set_redirection(client) != ESP_OK) {
            break;
        }
        esp_http_client_close(client);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    return NULL;
}

static void http_done(esp_http_client_handle_t client)
{
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

/* Downloads the feed at `url` into a new NUL-terminated PSRAM buffer
 * (*out_doc, caller frees). On failure, *out_error gets a short reason. */
static esp_err_t fetch_feed(const char *url, char **out_doc, const char **out_error)
{
    size_t len = 0;
    int status = 0;
    if (http_get_alloc(url, NULL, out_doc, &len, &status) != ESP_OK) {
        *out_error = "connect_failed";
        return ESP_FAIL;
    }
    if (status != 200 || len > FEED_MAX_LEN) {
        ESP_LOGW(TAG, "Feed %s returned HTTP %d (%u bytes)", url, status, (unsigned)len);
        free(*out_doc);
        *out_doc = NULL;
        *out_error = status == 404 ? "feed_not_found" : status != 200 ? "feed_http_error" : "feed_too_large";
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* Streams `url` into `path`, via a ".part" file that's only renamed into
 * place once the whole body has arrived - so the photo frame never sees (or
 * tries to decode) a half-written image, and a download interrupted by a
 * power cut is just retried next sync. */
static esp_err_t download_to_file(const char *url, const char *path)
{
    char part_path[PATH_MAX_LEN + 8];
    snprintf(part_path, sizeof(part_path), "%s.part", path);

    int status = 0;
    esp_http_client_handle_t client = http_open(url, &status);
    if (!client) {
        return ESP_FAIL;
    }
    if (status != 200) {
        ESP_LOGD(TAG, "%s returned HTTP %d", url, status);
        http_done(client);
        return ESP_ERR_NOT_FOUND;
    }

    FILE *f = fopen(part_path, "wb");
    uint8_t *buf = malloc(IO_CHUNK);
    if (!f || !buf) {
        ESP_LOGW(TAG, "Could not write %s", part_path);
        if (f) {
            fclose(f);
            remove(part_path);
        }
        free(buf);
        http_done(client);
        return ESP_FAIL;
    }

    bool ok = true;
    int total = 0;
    for (;;) {
        int r = esp_http_client_read(client, (char *)buf, IO_CHUNK);
        if (r < 0) {
            ok = false;
            break;
        }
        if (r == 0) {
            break;
        }
        total += r;
        if (total > IMAGE_MAX_LEN || fwrite(buf, 1, (size_t)r, f) != (size_t)r) {
            ok = false;
            break;
        }
    }
    /* read() also returns 0 on a timeout mid-body - only trust it as "done"
     * if the client agrees the whole response actually arrived. */
    ok = ok && total > 0 && esp_http_client_is_complete_data_received(client);
    free(buf);
    if (fclose(f) != 0) {
        ok = false;
    }
    http_done(client);

    if (!ok || rename(part_path, path) != 0) {
        ESP_LOGW(TAG, "Download of %s failed (%d bytes received)", url, total);
        remove(part_path);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Downloaded %s (%d bytes)", path, total);
    return ESP_OK;
}

/* ---------------------------------------------------------------------- */
/* SD card bookkeeping                                                     */
/* ---------------------------------------------------------------------- */

static bool in_list(const char *name, char (*list)[NAME_MAX_LEN], size_t count)
{
    for (size_t list_i = 0; list_i < count; list_i++) {
        if (strcmp(list[list_i], name) == 0) {
            return true;
        }
    }
    return false;
}

/* Deletes every image (and leftover ".part" file) in `dir` that isn't in
 * keep[] - pass keep_count 0 to empty it entirely. Names are collected
 * first and deleted after closedir(), rather than unlinked mid-readdir().
 * A file the photo frame is reading right now can't be deleted (FatFs file
 * lock) - it just goes on the next sync.
 * @return number of files deleted. */
static int prune_dir(const char *dir, char (*keep)[NAME_MAX_LEN], size_t keep_count)
{
    int removed = 0;
    size_t doomed_count;
    int batch_removed;
    do {
        doomed_count = 0;
        batch_removed = 0;
        DIR *d = opendir(dir);
        if (!d) {
            return removed;
        }
        struct dirent *entry;
        while ((entry = readdir(d)) != NULL && doomed_count < MAX_ITEMS) {
            if (entry->d_type == DT_DIR || strlen(entry->d_name) >= NAME_MAX_LEN) {
                continue;
            }
            if (!is_image_name(entry->d_name) && !has_extension(entry->d_name, ".part")) {
                continue;
            }
            if (!in_list(entry->d_name, keep, keep_count)) {
                strcpy(s_doomed[doomed_count++], entry->d_name);
            }
        }
        closedir(d);

        for (size_t doomed_i = 0; doomed_i < doomed_count; doomed_i++) {
            char path[PATH_MAX_LEN];
            snprintf(path, sizeof(path), "%s/%s", dir, s_doomed[doomed_i]);
            if (remove(path) == 0) {
                batch_removed++;
            }
        }
        removed += batch_removed;
        /* A full batch means there may be more - go again, unless nothing in
         * it could actually be deleted (which would just loop forever). */
    } while (doomed_count == MAX_ITEMS && batch_removed > 0);
    return removed;
}

static int count_images(const char *dir)
{
    int count = 0;
    DIR *d = opendir(dir);
    if (!d) {
        return 0;
    }
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (entry->d_type != DT_DIR && is_image_name(entry->d_name)) {
            count++;
        }
    }
    closedir(d);
    return count;
}

/* Removes the folders of feeds that are no longer configured.
 * @return number of image files deleted along with them. */
static int prune_removed_feeds(char (*hashes)[9], size_t hash_count)
{
    size_t orphan_count = 0;

    DIR *d = opendir(FLICKR_DIR);
    if (!d) {
        return 0;
    }
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL && orphan_count < MAX_ORPHAN_DIRS) {
        if (entry->d_type != DT_DIR || strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
            strlen(entry->d_name) >= NAME_MAX_LEN) {
            continue;
        }
        bool configured = false;
        for (size_t hash_i = 0; hash_i < hash_count && !configured; hash_i++) {
            configured = strcmp(hashes[hash_i], entry->d_name) == 0;
        }
        if (!configured) {
            strcpy(s_orphans[orphan_count++], entry->d_name);
        }
    }
    closedir(d);

    int removed = 0;
    for (size_t orphan_i = 0; orphan_i < orphan_count; orphan_i++) {
        char path[PATH_MAX_LEN];
        snprintf(path, sizeof(path), "%s/%s", FLICKR_DIR, s_orphans[orphan_i]);
        removed += prune_dir(path, NULL, 0);
        rmdir(path); /* best-effort: stays behind if something else was put in it (retried next sync) */
        ESP_LOGI(TAG, "Removed folder of deleted feed: %s", path);
    }
    return removed;
}

/* ---------------------------------------------------------------------- */
/* Sync                                                                    */
/* ---------------------------------------------------------------------- */

/* Brings `dir` in line with the feed at `feed_url`: downloads new images,
 * deletes ones that left the feed. Leaves the folder untouched if the feed
 * itself can't be read, so a network hiccup doesn't wipe the album.
 * @return ESP_OK if the feed and every image in it were fetched. */
static esp_err_t sync_feed(const char *feed_url, const char *dir, int *io_images, int *io_unsupported,
                           bool *io_changed, const char **out_error)
{
    char *doc = NULL;
    if (fetch_feed(feed_url, &doc, out_error) != ESP_OK) {
        *io_images += count_images(dir);
        return ESP_FAIL;
    }
    size_t url_count = parse_feed(doc, s_urls, MAX_ITEMS);
    free(doc);
    if (url_count == 0) {
        ESP_LOGW(TAG, "No images found in feed %s", feed_url);
        *out_error = "no_images_in_feed";
        *io_images += count_images(dir);
        return ESP_FAIL;
    }

    mkdir(dir, 0775);

    size_t keep_count = 0;
    int failures = 0;
    for (size_t url_i = 0; url_i < url_count; url_i++) {
        const char *orig_url = s_urls[url_i];
        char small_url[URL_MAX];
        strcpy(small_url, orig_url);
        prefer_800px(small_url);

        char small_name[NAME_MAX_LEN];
        char orig_name[NAME_MAX_LEN];
        url_to_name(small_url, small_name, sizeof(small_name));
        url_to_name(orig_url, orig_name, sizeof(orig_name));
        bool has_small = strcmp(small_name, orig_name) != 0;

        char small_path[PATH_MAX_LEN];
        char orig_path[PATH_MAX_LEN];
        if (snprintf(small_path, sizeof(small_path), "%s/%s", dir, small_name) >= (int)sizeof(small_path) ||
            snprintf(orig_path, sizeof(orig_path), "%s/%s", dir, orig_name) >= (int)sizeof(orig_path)) {
            failures++;
            continue;
        }

        const char *kept = NULL;
        const char *kept_path = NULL;
        if (file_exists(small_path)) {
            kept = small_name, kept_path = small_path;
        } else if (has_small && file_exists(orig_path)) {
            kept = orig_name, kept_path = orig_path;
        } else if (download_to_file(small_url, small_path) == ESP_OK) {
            kept = small_name, kept_path = small_path;
            *io_changed = true;
        } else if (has_small && download_to_file(orig_url, orig_path) == ESP_OK) {
            kept = orig_name, kept_path = orig_path;
            *io_changed = true;
        }

        if (kept) {
            strcpy(s_keep[keep_count++], kept);
            if (jpeg_is_progressive(kept_path)) {
                (*io_unsupported)++;
            }
        } else {
            failures++;
        }
    }

    if (prune_dir(dir, s_keep, keep_count) > 0) {
        *io_changed = true;
    }
    *io_images += (int)keep_count;

    if (failures > 0) {
        *out_error = "some_images_failed";
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void set_syncing(bool syncing)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.syncing = syncing;
    xSemaphoreGive(s_lock);
}

esp_err_t app_flickr_sync(bool online)
{
    if (!s_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!board_sdcard_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Snapshot the list: the web UI may save a new one during the (slow) sync. */
    settings_get_flickr_feeds(s_feeds, SETTINGS_FLICKR_FEEDS_LEN);
    if (online && s_feeds[0] == '\0') {
        online = false; /* nothing to download - just clean up */
    }

    set_syncing(true);
    struct stat st;
    if (stat(FLICKR_DIR, &st) != 0) {
        if (s_feeds[0] == '\0') {
            set_syncing(false);
            return ESP_OK; /* never used */
        }
        mkdir(PHOTOS_DIR, 0775);
        mkdir(FLICKR_DIR, 0775);
    }

    char hashes[SETTINGS_FLICKR_MAX_FEEDS][9];
    size_t feed_count = 0;
    int images = 0;
    int unsupported = 0;
    bool changed = false;
    char error[APP_FLICKR_ERROR_MAX] = "";

    char *save = NULL;
    for (char *url = strtok_r(s_feeds, "\n", &save); url && feed_count < SETTINGS_FLICKR_MAX_FEEDS;
         url = strtok_r(NULL, "\n", &save)) {
        if (url[0] == '\0') {
            continue;
        }
        feed_hash(url, hashes[feed_count]);
        char dir[PATH_MAX_LEN];
        snprintf(dir, sizeof(dir), "%s/%s", FLICKR_DIR, hashes[feed_count]);
        feed_count++;

        if (!online) {
            images += count_images(dir);
            continue;
        }
        const char *feed_error = NULL;
        ESP_LOGI(TAG, "Syncing feed %u: %s", (unsigned)feed_count, url);
        if (sync_feed(url, dir, &images, &unsupported, &changed, &feed_error) != ESP_OK && error[0] == '\0') {
            snprintf(error, sizeof(error), "feed %u: %s", (unsigned)feed_count, feed_error);
        }
    }

    if (prune_removed_feeds(hashes, feed_count) > 0) {
        changed = true;
    }
    if (changed) {
        app_event_post(APP_EVT_REQ_PHOTO_RESCAN, NULL, 0);
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.syncing = false;
    if (online) {
        s_status.have_error = error[0] != '\0';
        strlcpy(s_status.error, error, sizeof(s_status.error));
        s_status.last_sync = time(NULL);
        s_status.unsupported = unsupported;
    }
    s_status.image_count = images;
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "Sync done%s: %u feed(s), %d image(s), %d progressive%s%s", online ? "" : " (offline)",
             (unsigned)feed_count, images, unsupported, error[0] ? ", error: " : "", error);
    return error[0] ? ESP_FAIL : ESP_OK;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */
/* ---------------------------------------------------------------------- */

esp_err_t app_flickr_init(void)
{
    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    s_lock = xSemaphoreCreateMutex();
    s_feeds = heap_caps_malloc(SETTINGS_FLICKR_FEEDS_LEN, caps);
    s_urls = heap_caps_malloc(MAX_ITEMS * sizeof(*s_urls), caps);
    s_keep = heap_caps_malloc(MAX_ITEMS * sizeof(*s_keep), caps);
    s_doomed = heap_caps_malloc(MAX_ITEMS * sizeof(*s_doomed), caps);
    s_orphans = heap_caps_malloc(MAX_ORPHAN_DIRS * sizeof(*s_orphans), caps);
    if (!s_lock || !s_feeds || !s_urls || !s_keep || !s_doomed || !s_orphans) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_status, 0, sizeof(s_status));
    return ESP_OK;
}

bool app_flickr_has_feeds(void)
{
    char first[2];
    /* nvs_get_str fails with "too small" when a longer list exists - that also counts */
    return settings_get_flickr_feeds(first, sizeof(first)) != ESP_OK || first[0] != '\0';
}

void app_flickr_get_status(app_flickr_status_t *out)
{
    if (!s_lock) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_lock);
}
