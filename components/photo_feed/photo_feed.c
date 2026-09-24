/**
 * @file photo_feed.c
 * @brief See photo_feed.h. The feed is fetched with net's http_get_alloc();
 *        images are streamed straight to the SD card with esp_http_client
 *        open/read (+ crt bundle) and manual redirect handling, which
 *        open/read - unlike esp_http_client_perform() - doesn't do on its own.
 *
 * File names: an image URL that ends in "<name>.jpg/.jpeg/.png" keeps that
 * name (so Flickr's "<id>_<secret>_c.jpg" files survive the switch from the
 * old app_flickr), unless two items of the same feed share it. Everything
 * else - URLs without an extension, duplicates - is named "img_<url hash>".
 * The real type always comes from the file's first bytes, so the stored
 * extension is ".jpg" or ".png"; "<stem>.skip" marks an item whose image
 * is neither.
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

#include "photo_feed.h"
#include "app_state.h"
#include "app_storage.h"
#include "board.h"
#include "net.h"

static const char *TAG = "photo_feed";

#define PHOTOS_DIR        CONFIG_APP_PHOTO_DIR
#define FEEDS_DIR         PHOTO_FEED_DIR

#define FEED_MAX_LEN      (512 * 1024)          /* feeds with full article HTML can be a few hundred KB */
#define IMAGE_MAX_LEN     (4 * 1024 * 1024)     /* refuse anything absurd rather than fill the card */
#define MAX_ITEMS         50                    /* per feed - Flickr's own feeds return 20 */
#define URL_MAX           SETTINGS_FEED_URL_MAX
#define NAME_MAX_LEN      96
#define PATH_MAX_LEN      (sizeof(FEEDS_DIR) + 10 + NAME_MAX_LEN + 8)
#define MAX_REDIRECTS     5
#define IO_CHUNK          4096
#define MAX_ORPHAN_DIRS   16
#define USER_AGENT        "Mozilla/5.0 (ESP32-S3; SmartDisplay)"
#define SKIP_EXT          ".skip"

static SemaphoreHandle_t s_lock;
static photo_feed_status_t s_status;

/* Scratch buffers, only ever touched from net_worker - kept off its stack
 * (and in PSRAM: net_worker's stack is internal RAM, needed for TLS). */
static char *s_feeds;
static char (*s_urls)[URL_MAX];
static char (*s_segs)[NAME_MAX_LEN];     /* last path segment of each URL (duplicate check) */
static char (*s_keep)[NAME_MAX_LEN];
static char (*s_doomed)[NAME_MAX_LEN];
static char (*s_orphans)[NAME_MAX_LEN];

/* Per-image names/paths. The sync runs on net_worker's stack, and each image
 * download does a full TLS handshake on top of it - so these stay off it
 * (they were ~1.3KB there and overflowed the stack). */
typedef struct {
    char small_url[URL_MAX];
    char stem[NAME_MAX_LEN];
    char orig_stem[NAME_MAX_LEN];
    char found[NAME_MAX_LEN];
    char dir[PATH_MAX_LEN];
    char path[PATH_MAX_LEN + NAME_MAX_LEN + 8];        /* dir + "/" + file name */
    char part_path[PATH_MAX_LEN + NAME_MAX_LEN + 8];
} scratch_t;
static scratch_t *s_scratch;

typedef enum { IMG_NONE, IMG_JPEG, IMG_PNG } img_type_t;

/* ---------------------------------------------------------------------- */
/* Small helpers                                                           */
/* ---------------------------------------------------------------------- */

static bool has_extension(const char *name, const char *ext)
{
    size_t name_len = strlen(name);
    size_t ext_len = strlen(ext);
    return name_len >= ext_len && strcasecmp(name + (name_len - ext_len), ext) == 0;
}

/* Same set photo_loader plays. */
static bool is_image_name(const char *name)
{
    return has_extension(name, ".jpg") || has_extension(name, ".jpeg") || has_extension(name, ".png");
}

/* Extensions that are certainly not a still JPEG/PNG - skipped without downloading. */
static bool is_other_media_name(const char *name)
{
    static const char *const exts[] = { ".gif", ".webp", ".avif", ".heic", ".svg", ".bmp", ".mp3", ".m4a",
                                        ".mp4", ".m4v", ".mov", ".webm", ".ogg", ".pdf" };
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        if (has_extension(name, exts[i])) {
            return true;
        }
    }
    return false;
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

/* FNV-1a: short, stable names for feed folders and extension-less images. */
static uint32_t fnv1a(const char *s)
{
    uint32_t h = 2166136261u;
    for (const char *p = s; *p; p++) {
        h ^= (uint8_t)*p;
        h *= 16777619u;
    }
    return h;
}

static void feed_hash(const char *url, char out[9])
{
    snprintf(out, 9, "%08" PRIx32, fnv1a(url));
}

/* In-place decode of the entities that show up in URLs / escaped HTML. */
static void xml_unescape(char *s)
{
    static const struct { const char *ent; char ch; } ents[] = {
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''},
        {"&#38;", '&'}, {"&#34;", '"'}, {"&#39;", '\''},
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

/* Last path segment of a URL (query/fragment dropped), with anything FAT or a
 * path might choke on replaced by '_'. "" if there is none. */
static void url_segment(const char *url, char *out, size_t out_len)
{
    const char *end = url + strcspn(url, "?#");
    const char *start = end;
    while (start > url && start[-1] != '/') {
        start--;
    }
    size_t len = (size_t)(end - start);
    if (len >= out_len) {
        len = 0;
    }
    for (size_t char_i = 0; char_i < len; char_i++) {
        char c = start[char_i];
        out[char_i] = (isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_') ? c : '_';
    }
    out[len] = '\0';
    if (out[0] == '.') {
        out[0] = '\0';
    }
}

/* File name stem (no extension) for an image URL - see the file comment. */
static void image_stem(const char *url, const char *seg, bool seg_unique, char *out, size_t out_len)
{
    if (seg_unique && is_image_name(seg)) {
        strlcpy(out, seg, out_len);
        *strrchr(out, '.') = '\0';
    } else {
        snprintf(out, out_len, "img_%08" PRIx32, fnv1a(url));
    }
}

/* Looks for "<dir>/<stem>.jpg/.png/.jpeg/.skip"; copies the file name found. */
static bool find_existing(const char *dir, const char *stem, char *found, size_t found_len)
{
    static const char *const exts[] = { ".jpg", ".png", ".jpeg", SKIP_EXT };
    for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); i++) {
        snprintf(found, found_len, "%s%s", stem, exts[i]);
        snprintf(s_scratch->path, sizeof(s_scratch->path), "%s/%s", dir, found);
        if (file_exists(s_scratch->path)) {
            return true;
        }
    }
    return false;
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

/* Looks through every <tag ...> in [block, block_end) for attribute `attr`.
 * image_only: skip tags whose type (MIME) or medium attribute says they are
 * not an image. enclosure_only: Atom <link>, only rel="enclosure". */
static bool find_tag_url(const char *block, const char *block_end, const char *tag, const char *attr,
                         bool image_only, bool enclosure_only, char *out, size_t out_len)
{
    size_t tag_len = strlen(tag);
    for (const char *p = block; (p = strstr(p, tag)) != NULL && p < block_end; p += tag_len) {
        if (!isspace((unsigned char)p[tag_len])) {
            continue; /* a tag with no attributes, or just a longer tag name sharing this prefix */
        }
        const char *tag_end = strchr(p, '>');
        if (!tag_end || tag_end > block_end) {
            return false;
        }
        const char *attrs = p + tag_len;
        char value[32];
        if (enclosure_only && (!get_attr(attrs, tag_end, "rel", value, sizeof(value)) || strcmp(value, "enclosure") != 0)) {
            continue;
        }
        if (image_only) {
            if (get_attr(attrs, tag_end, "type", value, sizeof(value)) && strncmp(value, "image/", 6) != 0) {
                continue;
            }
            if (get_attr(attrs, tag_end, "medium", value, sizeof(value)) && strcmp(value, "image") != 0) {
                continue;
            }
        }
        if (get_attr(attrs, tag_end, attr, out, out_len)) {
            return true;
        }
    }
    return false;
}

/* First <img src> in entity-escaped HTML: "&lt;img ... src=&quot;URL&quot; ...&gt;"
 * (or src="URL" / src='URL'). The value is unescaped twice, since URLs in
 * escaped HTML carry "&amp;amp;". */
static bool find_escaped_img(const char *block, const char *block_end, char *out, size_t out_len)
{
    for (const char *p = block; (p = strstr(p, "&lt;img")) != NULL && p < block_end; p += 7) {
        const char *tag_end = strstr(p, "&gt;");
        if (!tag_end || tag_end > block_end) {
            return false;
        }
        const char *src = strstr(p, "src=");
        if (!src || src > tag_end) {
            continue;
        }
        src += 4;
        const char *delim;
        size_t delim_len;
        if (strncmp(src, "&quot;", 6) == 0) {
            delim = "&quot;", delim_len = 6;
        } else if (*src == '"' || *src == '\'') {
            delim = *src == '"' ? "\"" : "'", delim_len = 1;
        } else {
            continue;
        }
        const char *value = src + delim_len;
        const char *value_end = strstr(value, delim);
        if (!value_end || value_end > tag_end || (size_t)(value_end - value) >= out_len) {
            continue;
        }
        memcpy(out, value, (size_t)(value_end - value));
        out[value_end - value] = '\0';
        xml_unescape(out);
        xml_unescape(out);
        return true;
    }
    return false;
}

static bool is_http_url(const char *url)
{
    return strncmp(url, "https://", 8) == 0 || strncmp(url, "http://", 7) == 0;
}

/* Picks one image URL out of an <item>/<entry> block (see photo_feed.h). */
static bool item_image_url(const char *start, const char *end, char *url, size_t url_len)
{
    if (find_tag_url(start, end, "<media:content", "url", true, false, url, url_len) && is_http_url(url)) {
        return true;
    }
    if (find_tag_url(start, end, "<enclosure", "url", true, false, url, url_len) && is_http_url(url)) {
        return true;
    }
    if (find_tag_url(start, end, "<link", "href", true, true, url, url_len) && is_http_url(url)) {
        return true;
    }
    if (find_tag_url(start, end, "<media:thumbnail", "url", false, false, url, url_len) && is_http_url(url)) {
        return true;
    }
    /* HTML body: raw inside CDATA, or entity-escaped */
    if (find_tag_url(start, end, "<img", "src", false, false, url, url_len) && is_http_url(url)) {
        return true;
    }
    return find_escaped_img(start, end, url, url_len) && is_http_url(url);
}

/* Extracts each item's image URL from an RSS 2.0 (<item>) or Atom (<entry>) feed.
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
        p = end + strlen(close_tag);

        char *url = urls[count];
        if (!item_image_url(start, end, url, URL_MAX)) {
            continue;
        }
        url_segment(url, s_segs[count], NAME_MAX_LEN);
        if (is_other_media_name(s_segs[count])) {
            continue; /* GIF, WebP, video... */
        }
        bool dup = false;
        for (size_t url_i = 0; url_i < count && !dup; url_i++) {
            dup = strcmp(urls[url_i], url) == 0;
        }
        if (!dup) {
            count++;
        }
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
        /* CDNs send some long header lines (CSP, cookies) - the 512-byte
         * default buffers are too tight for them. */
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

static img_type_t sniff_image(const uint8_t *buf, int len)
{
    if (len >= 3 && buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) {
        return IMG_JPEG;
    }
    if (len >= 8 && memcmp(buf, "\x89PNG\r\n\x1a\n", 8) == 0) {
        return IMG_PNG;
    }
    return IMG_NONE;
}

/* Streams `url` into "<dir>/<stem>.jpg|.png" (type from the first bytes),
 * via a ".part" file that's only renamed into place once the whole body has
 * arrived - so the photo frame never sees (or tries to decode) a half-written
 * image, and a download interrupted by a power cut is just retried next sync.
 * Not an image: writes "<stem>.skip" instead and returns ESP_ERR_NOT_SUPPORTED.
 * The file name used is copied to out_name. */
static esp_err_t download_to_file(const char *url, const char *dir, const char *stem, char *out_name, size_t name_len)
{
    char *part_path = s_scratch->part_path;
    snprintf(part_path, sizeof(s_scratch->part_path), "%s/%s.part", dir, stem);

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
    img_type_t type = IMG_NONE;
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
        if (total == 0) {
            type = sniff_image(buf, r);
            if (type == IMG_NONE) {
                break; /* HTML page, WebP, GIF... - no need to read the rest */
            }
        }
        total += r;
        if (total > IMAGE_MAX_LEN || fwrite(buf, 1, (size_t)r, f) != (size_t)r) {
            ok = false;
            break;
        }
    }
    /* read() also returns 0 on a timeout mid-body - only trust it as "done"
     * if the client agrees the whole response actually arrived. */
    ok = ok && (type == IMG_NONE || (total > 0 && esp_http_client_is_complete_data_received(client)));
    free(buf);
    if (fclose(f) != 0) {
        ok = false;
    }
    http_done(client);

    if (ok && type == IMG_NONE) {
        remove(part_path);
        snprintf(out_name, name_len, "%s%s", stem, SKIP_EXT);
        snprintf(s_scratch->path, sizeof(s_scratch->path), "%s/%s", dir, out_name);
        FILE *marker = fopen(s_scratch->path, "wb");   /* empty marker: don't fetch it again */
        if (marker) {
            fclose(marker);
        }
        ESP_LOGI(TAG, "%s is not a JPEG/PNG image, skipped", url);
        return ESP_ERR_NOT_SUPPORTED;
    }
    snprintf(out_name, name_len, "%s%s", stem, type == IMG_PNG ? ".png" : ".jpg");
    snprintf(s_scratch->path, sizeof(s_scratch->path), "%s/%s", dir, out_name);
    if (!ok || rename(part_path, s_scratch->path) != 0) {
        ESP_LOGW(TAG, "Download of %s failed (%d bytes received)", url, total);
        remove(part_path);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Downloaded %s (%d bytes)", s_scratch->path, total);
    return ESP_OK;
}

/* ---------------------------------------------------------------------- */
/* SD card bookkeeping                                                     */
/* ---------------------------------------------------------------------- */

static bool in_list(const char *name, char (*list)[NAME_MAX_LEN], size_t count)
{
    for (size_t list_i = 0; list_i < count; list_i++) {
        if (strcasecmp(list[list_i], name) == 0) {
            return true;
        }
    }
    return false;
}

/* Deletes every image, ".skip" marker and leftover ".part" file in `dir`
 * that isn't in keep[] - pass keep_count 0 to empty it entirely. Names are
 * collected first and deleted after closedir(), rather than unlinked
 * mid-readdir(). A file the photo frame is reading right now can't be
 * deleted (FatFs file lock) - it just goes on the next sync.
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
            if (!is_image_name(entry->d_name) && !has_extension(entry->d_name, ".part") &&
                !has_extension(entry->d_name, SKIP_EXT)) {
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
 * @return number of files deleted along with them. */
static int prune_removed_feeds(char (*hashes)[9], size_t hash_count)
{
    size_t orphan_count = 0;

    DIR *d = opendir(FEEDS_DIR);
    if (!d) {
        return 0;
    }
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL && orphan_count < MAX_ORPHAN_DIRS) {
        if (entry->d_type != DT_DIR || entry->d_name[0] == '.' || strlen(entry->d_name) >= NAME_MAX_LEN) {
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
        snprintf(path, sizeof(path), "%s/%s", FEEDS_DIR, s_orphans[orphan_i]);
        removed += prune_dir(path, NULL, 0);
        rmdir(path); /* best-effort: stays behind if something else was put in it (retried next sync) */
        ESP_LOGI(TAG, "Removed folder of deleted feed: %s", path);
    }
    return removed;
}

/* Up to v0.2.2 (app_flickr) the mirror lived in photos/flickr. Renaming the
 * folder keeps the downloaded images (same feed hashes, same file names). */
static void migrate_old_dir(void)
{
    static bool s_done;
    if (s_done) {
        return;
    }
    s_done = true;
    struct stat st;
    if (stat(PHOTO_FEED_OLD_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return;
    }
    if (stat(FEEDS_DIR, &st) == 0) {
        ESP_LOGW(TAG, "Both %s and %s exist - leaving the old folder alone", PHOTO_FEED_OLD_DIR, FEEDS_DIR);
        return;
    }
    if (rename(PHOTO_FEED_OLD_DIR, FEEDS_DIR) == 0) {
        ESP_LOGI(TAG, "Moved %s to %s", PHOTO_FEED_OLD_DIR, FEEDS_DIR);
        app_event_post(APP_EVT_REQ_PHOTO_RESCAN, NULL, 0);
    } else {
        ESP_LOGW(TAG, "Could not rename %s to %s", PHOTO_FEED_OLD_DIR, FEEDS_DIR);
    }
}

/* ---------------------------------------------------------------------- */
/* Sync                                                                    */
/* ---------------------------------------------------------------------- */

typedef struct {
    int  images;
    int  unsupported;
    int  skipped;
    bool changed;
} sync_counts_t;

/* Counts a kept file into the totals. */
static void count_kept(const char *dir, const char *name, sync_counts_t *io)
{
    if (has_extension(name, SKIP_EXT)) {
        io->skipped++;
        return;
    }
    io->images++;
    snprintf(s_scratch->path, sizeof(s_scratch->path), "%s/%s", dir, name);
    if (jpeg_is_progressive(s_scratch->path)) {
        io->unsupported++;
    }
}

/* Brings `dir` in line with the feed at `feed_url`: downloads new images,
 * deletes ones that left the feed. Leaves the folder untouched if the feed
 * itself can't be read, so a network hiccup doesn't wipe the album.
 * @return ESP_OK if the feed and every image in it were fetched. */
static esp_err_t sync_feed(const char *feed_url, const char *dir, sync_counts_t *io, const char **out_error)
{
    char *doc = NULL;
    if (fetch_feed(feed_url, &doc, out_error) != ESP_OK) {
        io->images += count_images(dir);
        return ESP_FAIL;
    }
    size_t url_count = parse_feed(doc, s_urls, MAX_ITEMS);
    free(doc);
    if (url_count == 0) {
        ESP_LOGW(TAG, "No images found in feed %s", feed_url);
        *out_error = "no_images_in_feed";
        io->images += count_images(dir);
        return ESP_FAIL;
    }

    mkdir(dir, 0775);

    size_t keep_count = 0;
    int failures = 0;
    for (size_t url_i = 0; url_i < url_count; url_i++) {
        const char *orig_url = s_urls[url_i];
        bool unique = true;
        for (size_t other = 0; other < url_count && unique; other++) {
            unique = other == url_i || strcasecmp(s_segs[other], s_segs[url_i]) != 0;
        }

        char *small_url = s_scratch->small_url;
        strcpy(small_url, orig_url);
        prefer_800px(small_url);
        bool has_small = strcmp(small_url, orig_url) != 0;

        char *stem = s_scratch->stem;
        char *orig_stem = s_scratch->orig_stem;
        char seg[NAME_MAX_LEN];
        url_segment(small_url, seg, sizeof(seg));
        image_stem(small_url, seg, unique, stem, NAME_MAX_LEN);
        image_stem(orig_url, s_segs[url_i], unique, orig_stem, NAME_MAX_LEN);

        char *found = s_scratch->found;
        bool kept = false;
        if (find_existing(dir, stem, found, NAME_MAX_LEN) ||
            (has_small && find_existing(dir, orig_stem, found, NAME_MAX_LEN))) {
            kept = true;
        } else {
            esp_err_t err = download_to_file(small_url, dir, stem, found, NAME_MAX_LEN);
            if ((err == ESP_FAIL || err == ESP_ERR_NOT_FOUND) && has_small) {
                err = download_to_file(orig_url, dir, orig_stem, found, NAME_MAX_LEN);
            }
            kept = err == ESP_OK || err == ESP_ERR_NOT_SUPPORTED;   /* not an image: the .skip marker */
            io->changed |= err == ESP_OK;
        }

        if (kept) {
            strlcpy(s_keep[keep_count++], found, NAME_MAX_LEN);
            count_kept(dir, found, io);
        } else {
            failures++;
        }
    }

    if (prune_dir(dir, s_keep, keep_count) > 0) {
        io->changed = true;
    }

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

esp_err_t photo_feed_sync(bool online)
{
    if (!s_lock) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!board_sdcard_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    migrate_old_dir();

    /* Snapshot the list: the web UI may save a new one during the (slow) sync. */
    settings_get_photo_feeds(s_feeds, SETTINGS_FEED_FEEDS_LEN);
    if (online && s_feeds[0] == '\0') {
        online = false; /* nothing to download - just clean up */
    }

    set_syncing(true);
    struct stat st;
    if (stat(FEEDS_DIR, &st) != 0) {
        if (s_feeds[0] == '\0') {
            set_syncing(false);
            return ESP_OK; /* never used */
        }
        mkdir(PHOTOS_DIR, 0775);
        mkdir(FEEDS_DIR, 0775);
    }

    char hashes[SETTINGS_FEED_MAX_FEEDS][9];
    size_t feed_count = 0;
    sync_counts_t counts = { 0 };
    char error[PHOTO_FEED_ERROR_MAX] = "";

    char *save = NULL;
    for (char *url = strtok_r(s_feeds, "\n", &save); url && feed_count < SETTINGS_FEED_MAX_FEEDS;
         url = strtok_r(NULL, "\n", &save)) {
        if (url[0] == '\0') {
            continue;
        }
        feed_hash(url, hashes[feed_count]);
        char *dir = s_scratch->dir;
        snprintf(dir, sizeof(s_scratch->dir), "%s/%s", FEEDS_DIR, hashes[feed_count]);
        feed_count++;

        if (!online) {
            counts.images += count_images(dir);
            continue;
        }
        const char *feed_error = NULL;
        ESP_LOGI(TAG, "Syncing feed %u: %s", (unsigned)feed_count, url);
        if (sync_feed(url, dir, &counts, &feed_error) != ESP_OK && error[0] == '\0') {
            snprintf(error, sizeof(error), "feed %u: %s", (unsigned)feed_count, feed_error);
        }
    }

    if (prune_removed_feeds(hashes, feed_count) > 0) {
        counts.changed = true;
    }
    if (counts.changed) {
        app_event_post(APP_EVT_REQ_PHOTO_RESCAN, NULL, 0);
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_status.syncing = false;
    if (online) {
        s_status.have_error = error[0] != '\0';
        strlcpy(s_status.error, error, sizeof(s_status.error));
        s_status.last_sync = time(NULL);
        s_status.unsupported = counts.unsupported;
        s_status.skipped = counts.skipped;
    }
    s_status.image_count = counts.images;
    xSemaphoreGive(s_lock);

    ESP_LOGI(TAG, "Sync done%s: %u feed(s), %d image(s), %d progressive, %d skipped%s%s", online ? "" : " (offline)",
             (unsigned)feed_count, counts.images, counts.unsupported, counts.skipped, error[0] ? ", error: " : "", error);
    return error[0] ? ESP_FAIL : ESP_OK;
}

/* ---------------------------------------------------------------------- */
/* Public API                                                              */
/* ---------------------------------------------------------------------- */

esp_err_t photo_feed_init(void)
{
    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    s_lock = xSemaphoreCreateMutex();
    s_feeds = heap_caps_malloc(SETTINGS_FEED_FEEDS_LEN, caps);
    s_urls = heap_caps_malloc(MAX_ITEMS * sizeof(*s_urls), caps);
    s_segs = heap_caps_malloc(MAX_ITEMS * sizeof(*s_segs), caps);
    s_keep = heap_caps_malloc(MAX_ITEMS * sizeof(*s_keep), caps);
    s_doomed = heap_caps_malloc(MAX_ITEMS * sizeof(*s_doomed), caps);
    s_orphans = heap_caps_malloc(MAX_ORPHAN_DIRS * sizeof(*s_orphans), caps);
    s_scratch = heap_caps_malloc(sizeof(*s_scratch), caps);
    if (!s_lock || !s_feeds || !s_urls || !s_segs || !s_keep || !s_doomed || !s_orphans || !s_scratch) {
        return ESP_ERR_NO_MEM;
    }
    memset(&s_status, 0, sizeof(s_status));
    return ESP_OK;
}

bool photo_feed_has_feeds(void)
{
    char first[2];
    /* nvs_get_str fails with "too small" when a longer list exists - that also counts */
    return settings_get_photo_feeds(first, sizeof(first)) != ESP_OK || first[0] != '\0';
}

void photo_feed_get_status(photo_feed_status_t *out)
{
    if (!s_lock) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_lock);
}
