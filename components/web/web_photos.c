// 전자앨범 사진 관리 API (SD 카드의 CONFIG_APP_PHOTO_DIR)
//   앨범 = 사진 폴더 바로 아래의 폴더 ("" = 사진 폴더 자체). 더 깊은 하위 폴더는 전자앨범에는 나오지만 웹에서는 보이지 않는다.
//   GET  /api/photos                                   {sd, albums:[{name, count}]}
//   GET  /api/photos/list?album=                       {photos:[name, ...]}
//   GET  /api/photos/file?album=&name=[&thumb=1]       이미지 (썸네일이 없으면 원본). <img> 용으로 ?pin= 도 허용
//   POST /api/photos/upload?album=&name=[&thumb=1]     본문 = JPEG. 같은 이름이 있으면 "이름-1.jpg" 로 바꿔 저장 → {name}
//   POST /api/photos/delete?album=&name=               사진 + 썸네일 삭제, 앨범이 비면 폴더도 삭제
// flickr 폴더는 app_flickr 의 미러라 앨범 목록에서 빼고 요청도 거부한다 (웹 페이지의 Flickr 카드에서 관리).
// 썸네일은 <앨범>/.thumbs/<이름> 에 둔다 (전자앨범은 '.' 로 시작하는 폴더를 건너뛴다).
// 받는 중인 파일은 "<이름>.part" 로 쓰고 끝나면 이름을 바꾸므로, 전자앨범이 반쯤 받은 파일을 읽지 않는다.
// 업로드/삭제 후 3초 동안 더 바뀌지 않으면 APP_EVT_REQ_PHOTO_RESCAN 을 보낸다 (여러 장 올려도 한 번만 다시 읽음).

#include "web_internal.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "app_state.h"
#include "board.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#define PHOTO_DIR           CONFIG_APP_PHOTO_DIR
#define THUMB_DIR           ".thumbs"
#define NAME_MAX_BYTES      80                      // UTF-8 바이트 (FAT 긴 이름 255자 안쪽)
#define QUERY_BUF_LEN       (NAME_MAX_BYTES * 3 + 1)  // URL 인코딩된 값도 담을 수 있게
// 경로 버퍼: 확인 전의 쿼리 값(QUERY_BUF_LEN)이 들어가도 잘리지 않는 크기 (snprintf 경고 방지)
#define PATH_LEN            (sizeof(PHOTO_DIR) + 2 * QUERY_BUF_LEN + sizeof(THUMB_DIR) + 8)
#define UPLOAD_MAX_LEN      (10 * 1024 * 1024)
#define IO_CHUNK            (8 * 1024)
#define LIST_MAX            2000
#define RECV_TIMEOUTS_MAX   3
#define RESCAN_DELAY_US     (3 * 1000 * 1000)

static const char *TAG = "web_photos";

static esp_timer_handle_t s_rescan_timer;

// 요청 하나의 이름/경로 버퍼 (스택이 작아 힙에 둔다)
typedef struct {
    char album[QUERY_BUF_LEN];
    char name[QUERY_BUF_LEN];
    char path[PATH_LEN];
    char tmp[PATH_LEN + 256];   // 경로 + ".part" 또는 폴더 경로 + 항목 이름
    bool thumb;
} photo_req_t;

// ---- 이름 / 경로 ----

static bool has_ext(const char *name, const char *ext)
{
    const char *dot = strrchr(name, '.');
    return dot && strcasecmp(dot + 1, ext) == 0;
}

static bool is_photo(const char *name)   // photo_loader 와 같은 기준
{
    return name[0] != '.' && (has_ext(name, "jpg") || has_ext(name, "jpeg") || has_ext(name, "png"));
}

// 숨김/시스템 폴더와 Flickr 미러 폴더 (app_flickr 가 관리: 여기에 올리거나 지우면 다음 동기화 때 되돌아간다)
static bool skip_dir(const char *name)
{
    return name[0] == '.' || strcasecmp(name, "System Volume Information") == 0 || strcasecmp(name, "flickr") == 0;
}

// 파일/폴더 이름 하나로 쓸 수 있는지 (경로 구분자, FAT 금지 문자, 숨김 이름 거부)
static bool valid_name(const char *s, bool allow_empty)
{
    size_t len = strlen(s);
    if (len == 0) {
        return allow_empty;
    }
    if (len > NAME_MAX_BYTES || s[0] == '.' || s[0] == ' ' || s[len - 1] == ' ' || s[len - 1] == '.') {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p < 0x20 || strchr("/\\:*?\"<>|", *p)) {
            return false;
        }
    }
    return true;
}

static void album_dir(const char *album, bool thumb, char *out, size_t size)
{
    snprintf(out, size, "%s%s%s%s", PHOTO_DIR, album[0] ? "/" : "", album, thumb ? "/" THUMB_DIR : "");
}

static void photo_path(const photo_req_t *r, bool thumb, char *out, size_t size)
{
    album_dir(r->album, thumb, out, size);
    size_t len = strlen(out);
    snprintf(out + len, size - len, "/%s", r->name);
}

static bool file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static int count_photos(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) {
        return 0;
    }
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_type != DT_DIR && is_photo(e->d_name)) {
            n++;
        }
    }
    closedir(d);
    return n;
}

// album, name, thumb 쿼리를 읽고 확인한다. 실패하면 오류 응답을 보내고 NULL.
static photo_req_t *parse_request(httpd_req_t *req, bool need_name)
{
    if (!board_sdcard_is_mounted()) {
        web_send_error(req, "503 Service Unavailable", "no sd card");
        return NULL;
    }
    photo_req_t *r = calloc(1, sizeof(photo_req_t));
    if (!r) {
        web_send_error(req, "500 Internal Server Error", "no memory");
        return NULL;
    }
    web_query_param(req, "album", r->album, sizeof(r->album));
    web_query_param(req, "name", r->name, sizeof(r->name));
    char thumb[4];
    r->thumb = web_query_param(req, "thumb", thumb, sizeof(thumb)) && strcmp(thumb, "1") == 0;

    if (!valid_name(r->album, true) || (r->album[0] && skip_dir(r->album))) {
        web_send_error(req, "400 Bad Request", "album");
    } else if (need_name && (!valid_name(r->name, false) || !is_photo(r->name))) {
        web_send_error(req, "400 Bad Request", "name");
    } else {
        return r;
    }
    free(r);
    return NULL;
}

static void on_rescan_timer(void *arg)
{
    app_event_post(APP_EVT_REQ_PHOTO_RESCAN, NULL, 0);
}

static void schedule_rescan(void)
{
    esp_timer_stop(s_rescan_timer);   // 실행 중이 아니면 오류를 반환하지만 무시
    esp_timer_start_once(s_rescan_timer, RESCAN_DELAY_US);
}

// ---- 핸들러 ----

static esp_err_t get_albums(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return ESP_OK;
    }
    cJSON *root = cJSON_CreateObject();
    bool sd = board_sdcard_is_mounted();
    cJSON_AddBoolToObject(root, "sd", sd);
    cJSON *list = cJSON_AddArrayToObject(root, "albums");
    if (!sd) {
        return web_send_json(req, root);
    }

    cJSON *item = cJSON_CreateObject();   // 사진 폴더 자체 (항상 첫 번째)
    cJSON_AddStringToObject(item, "name", "");
    cJSON_AddNumberToObject(item, "count", count_photos(PHOTO_DIR));
    cJSON_AddItemToArray(list, item);

    char *path = malloc(PATH_LEN);
    DIR *d = path ? opendir(PHOTO_DIR) : NULL;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (e->d_type != DT_DIR || skip_dir(e->d_name) || strlen(e->d_name) > NAME_MAX_BYTES) {
                continue;
            }
            if (snprintf(path, PATH_LEN, "%s/%s", PHOTO_DIR, e->d_name) >= (int)PATH_LEN) {
                continue;
            }
            item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "name", e->d_name);
            cJSON_AddNumberToObject(item, "count", count_photos(path));
            cJSON_AddItemToArray(list, item);
        }
        closedir(d);
    }
    free(path);
    return web_send_json(req, root);
}

static esp_err_t get_list(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return ESP_OK;
    }
    photo_req_t *r = parse_request(req, false);
    if (!r) {
        return ESP_OK;
    }
    album_dir(r->album, false, r->path, sizeof(r->path));
    cJSON *root = cJSON_CreateObject();
    cJSON *list = cJSON_AddArrayToObject(root, "photos");
    DIR *d = opendir(r->path);
    if (d) {
        struct dirent *e;
        int n = 0;
        while ((e = readdir(d)) && n < LIST_MAX) {
            if (e->d_type != DT_DIR && is_photo(e->d_name)) {
                cJSON_AddItemToArray(list, cJSON_CreateString(e->d_name));
                n++;
            }
        }
        closedir(d);
    }
    free(r);
    return web_send_json(req, root);
}

static esp_err_t get_file(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return ESP_OK;
    }
    photo_req_t *r = parse_request(req, true);
    if (!r) {
        return ESP_OK;
    }
    FILE *f = NULL;
    if (r->thumb) {
        photo_path(r, true, r->path, sizeof(r->path));
        f = fopen(r->path, "rb");
    }
    if (!f) {   // 썸네일이 없으면 (PC 에서 직접 넣은 사진) 원본
        photo_path(r, false, r->path, sizeof(r->path));
        f = fopen(r->path, "rb");
    }
    char *buf = f ? malloc(IO_CHUNK) : NULL;
    if (!buf) {
        if (f) {
            fclose(f);
        }
        esp_err_t err = f ? web_send_error(req, "500 Internal Server Error", "no memory")
                          : web_send_error(req, "404 Not Found", "not found");
        free(r);
        return err;
    }

    httpd_resp_set_type(req, has_ext(r->name, "png") ? "image/png" : "image/jpeg");
    httpd_resp_set_hdr(req, "Cache-Control", "private, max-age=3600");
    esp_err_t err = ESP_OK;
    size_t n;
    while ((n = fread(buf, 1, IO_CHUNK, f)) > 0) {
        err = httpd_resp_send_chunk(req, buf, n);
        if (err != ESP_OK) {
            break;   // 브라우저가 연결을 닫음 (다른 앨범으로 이동 등)
        }
    }
    fclose(f);
    free(buf);
    free(r);
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, NULL, 0);
    }
    return err;
}

// 같은 이름이 있으면 "이름-1.jpg", "이름-2.jpg" ... 중 비어 있는 이름으로 바꾼다
static bool pick_unique_name(photo_req_t *r)
{
    photo_path(r, false, r->path, sizeof(r->path));
    if (!file_exists(r->path)) {
        return true;
    }
    char base[QUERY_BUF_LEN];
    strlcpy(base, r->name, sizeof(base));
    char *dot = strrchr(base, '.');
    char ext[8];
    strlcpy(ext, dot, sizeof(ext));
    *dot = '\0';
    for (int i = 1; i < 1000; i++) {
        if (snprintf(r->name, sizeof(r->name), "%s-%d%s", base, i, ext) >= (int)sizeof(r->name)) {
            return false;
        }
        photo_path(r, false, r->path, sizeof(r->path));
        if (!file_exists(r->path)) {
            return true;
        }
    }
    return false;
}

// 본문을 tmp 파일로 받는다. 실패하면 tmp 를 지우고 오류 응답까지 보낸다.
static bool receive_to_file(httpd_req_t *req, const char *tmp)
{
    char *buf = malloc(IO_CHUNK);
    FILE *f = buf ? fopen(tmp, "wb") : NULL;
    if (!f) {
        free(buf);
        web_send_error(req, "500 Internal Server Error", buf ? "open failed" : "no memory");
        return false;
    }
    const char *error = NULL;
    size_t got = 0;
    int timeouts = 0;
    while (got < req->content_len && !error) {
        size_t want = req->content_len - got < IO_CHUNK ? req->content_len - got : IO_CHUNK;
        int n = httpd_req_recv(req, buf, want);
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts <= RECV_TIMEOUTS_MAX) {
            continue;
        }
        if (n <= 0) {
            error = "receive failed";
            break;
        }
        // 브라우저가 만든 JPEG 만 받는다 (기기 디코더가 풀 수 있는 형식)
        if (got == 0 && (n < 3 || (uint8_t)buf[0] != 0xFF || (uint8_t)buf[1] != 0xD8 || (uint8_t)buf[2] != 0xFF)) {
            error = "not a jpeg";
            break;
        }
        if (fwrite(buf, 1, n, f) != (size_t)n) {
            error = "write failed (sd card full?)";
            break;
        }
        got += n;
    }
    if (fclose(f) != 0 && !error) {
        error = "write failed";
    }
    free(buf);
    if (error) {
        unlink(tmp);
        web_send_error(req, "500 Internal Server Error", error);
        return false;
    }
    return true;
}

static esp_err_t post_upload(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return ESP_OK;
    }
    if (req->content_len == 0 || req->content_len > UPLOAD_MAX_LEN) {
        return web_send_error(req, "413 Payload Too Large", "size");
    }
    photo_req_t *r = parse_request(req, true);
    if (!r) {
        return ESP_OK;
    }
    if (!has_ext(r->name, "jpg") && !has_ext(r->name, "jpeg")) {
        free(r);
        return web_send_error(req, "400 Bad Request", "name");
    }

    mkdir(PHOTO_DIR, 0775);   // 이미 있으면 실패하지만 무시
    album_dir(r->album, false, r->path, sizeof(r->path));
    mkdir(r->path, 0775);
    if (r->thumb) {
        album_dir(r->album, true, r->path, sizeof(r->path));
        mkdir(r->path, 0775);
        photo_path(r, true, r->path, sizeof(r->path));   // 썸네일은 같은 이름을 덮어쓴다
    } else if (!pick_unique_name(r)) {
        free(r);
        return web_send_error(req, "409 Conflict", "name");
    }
    snprintf(r->tmp, sizeof(r->tmp), "%s.part", r->path);

    if (!receive_to_file(req, r->tmp)) {
        free(r);
        return ESP_OK;
    }
    if (r->thumb) {
        unlink(r->path);   // FAT 의 rename 은 대상이 있으면 실패한다
    }
    if (rename(r->tmp, r->path) != 0) {
        unlink(r->tmp);
        free(r);
        return web_send_error(req, "500 Internal Server Error", "rename failed");
    }
    ESP_LOGI(TAG, "uploaded %s (%u bytes)", r->path, (unsigned)req->content_len);
    if (!r->thumb) {
        schedule_rescan();
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", r->name);
    free(r);
    return web_send_json(req, root);
}

// 앨범 폴더가 비었으면 썸네일 폴더와 함께 지운다 (다른 파일/폴더가 남아 있으면 rmdir 이 실패하므로 그대로 둔다)
static void remove_album_if_empty(photo_req_t *r)
{
    album_dir(r->album, false, r->path, sizeof(r->path));
    if (!r->album[0] || count_photos(r->path) > 0) {
        return;
    }
    album_dir(r->album, true, r->path, sizeof(r->path));
    // readdir 도중에 지우지 않도록 한 번에 하나씩: 첫 항목을 읽고 닫은 뒤 삭제
    for (int i = 0; i < LIST_MAX; i++) {
        DIR *d = opendir(r->path);
        struct dirent *e = d ? readdir(d) : NULL;
        if (e) {
            snprintf(r->tmp, sizeof(r->tmp), "%s/%s", r->path, e->d_name);
        }
        if (d) {
            closedir(d);
        }
        if (!e || unlink(r->tmp) != 0) {
            break;
        }
    }
    rmdir(r->path);
    album_dir(r->album, false, r->path, sizeof(r->path));
    if (rmdir(r->path) == 0) {
        ESP_LOGI(TAG, "removed empty album %s", r->path);
    }
}

static esp_err_t post_delete(httpd_req_t *req)
{
    if (!web_authorized(req)) {
        return ESP_OK;
    }
    photo_req_t *r = parse_request(req, true);
    if (!r) {
        return ESP_OK;
    }
    photo_path(r, false, r->path, sizeof(r->path));
    if (unlink(r->path) != 0) {
        int e = errno;
        free(r);
        // EBUSY: 전자앨범이 지금 이 파일을 읽는 중 (FATFS_FS_LOCK) → 브라우저가 잠시 후 다시 시도
        return e == ENOENT ? web_send_error(req, "404 Not Found", "not found")
             : e == EBUSY  ? web_send_error(req, "409 Conflict", "busy")
                           : web_send_error(req, "500 Internal Server Error", "delete failed");
    }
    photo_path(r, true, r->path, sizeof(r->path));
    unlink(r->path);   // 썸네일 (없을 수 있음)
    ESP_LOGI(TAG, "deleted %s/%s", r->album[0] ? r->album : ".", r->name);
    remove_album_if_empty(r);
    free(r);
    schedule_rescan();
    return httpd_resp_sendstr(req, "{\"ok\":true}");
}

esp_err_t web_photos_register(httpd_handle_t server)
{
    const esp_timer_create_args_t timer_args = { .callback = on_rescan_timer, .name = "photo_rescan" };
    ESP_RETURN_ON_ERROR(esp_timer_create(&timer_args, &s_rescan_timer), TAG, "timer");

    static const httpd_uri_t uris[] = {
        { .uri = "/api/photos",        .method = HTTP_GET,  .handler = get_albums },
        { .uri = "/api/photos/list",   .method = HTTP_GET,  .handler = get_list },
        { .uri = "/api/photos/file",   .method = HTTP_GET,  .handler = get_file },
        { .uri = "/api/photos/upload", .method = HTTP_POST, .handler = post_upload },
        { .uri = "/api/photos/delete", .method = HTTP_POST, .handler = post_delete },
    };
    for (size_t i = 0; i < sizeof(uris) / sizeof(uris[0]); i++) {
        ESP_RETURN_ON_ERROR(httpd_register_uri_handler(server, &uris[i]), TAG, "uri %s", uris[i].uri);
    }
    return ESP_OK;
}
