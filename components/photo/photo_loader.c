#include "photo.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "app_state.h"
#include "app_storage.h"
#include "board.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/tjpgd.h"
#include "lvgl.h"
#include "libs/lodepng/lodepng.h"

#define MAX_FILES           2000       // 경로 문자열은 PSRAM (평균 60B x 2000 = 약 120KB)
#define MAX_DEPTH           4          // 하위 폴더 탐색 깊이
#define PATH_MAX_LEN        256
#define FRAME_PIXELS        (BOARD_LCD_H_RES * BOARD_LCD_V_RES)   // 회전과 무관하게 화면 면적
#define TJPGD_POOL_SIZE     4096
#define FILE_BUF_SIZE       (16 * 1024)
#define PNG_MAX_PIXELS      (800 * 600)
#define PNG_MAX_FILE        (2 * 1024 * 1024)
#define EXIF_MAX_SEGMENT    (64 * 1024)
#define EMPTY_RESCAN_MS     (60 * 1000)

#define NOTIFY_NEXT         BIT0
#define NOTIFY_PREV         BIT1
#define NOTIFY_SETTINGS     BIT2
#define NOTIFY_RESCAN       BIT3

static const char *TAG = "photo";

static TaskHandle_t  s_task;
static photo_frame_t s_frames[2];
static char          s_dir[64];
static int           s_interval_s;
static uint16_t      s_max_w, s_max_h;

static char        **s_files;      // PSRAM 에 보관하는 파일 경로 목록 (정렬됨)
static int           s_count;

// ---------------------------------------------------------------------------
// 파일 목록
// ---------------------------------------------------------------------------

static bool has_ext(const char *name, const char *ext)
{
    const char *dot = strrchr(name, '.');
    return dot && strcasecmp(dot + 1, ext) == 0;
}

static bool is_photo(const char *name)
{
    return name[0] != '.' && (has_ext(name, "jpg") || has_ext(name, "jpeg") || has_ext(name, "png"));
}

static int cmp_names(const void *a, const void *b)
{
    return strcasecmp(*(char *const *)a, *(char *const *)b);
}

static void free_list(void)
{
    for (int i = 0; i < s_count; i++) {
        free(s_files[i]);
    }
    s_count = 0;
}

static bool skip_dir(const char *name)
{
    // 숨김/시스템 폴더 (Windows 가 SD 카드에 만드는 폴더 포함)
    return name[0] == '.' || strcasecmp(name, "System Volume Information") == 0;
}

// dir 과 그 하위 폴더(최대 MAX_DEPTH 단계)의 사진 경로를 목록에 추가한다.
// path 는 PATH_MAX_LEN 크기의 공용 버퍼로, 재귀하면서 뒤에 이어 붙였다가 되돌린다.
static void scan_recursive(char *path, int depth)
{
    DIR *d = opendir(path);
    if (!d) {
        return;
    }
    size_t base = strlen(path);
    struct dirent *e;
    while ((e = readdir(d)) && s_count < MAX_FILES) {
        bool is_dir = e->d_type == DT_DIR;
        if (is_dir ? (depth >= MAX_DEPTH || skip_dir(e->d_name)) : !is_photo(e->d_name)) {
            continue;
        }
        if (base + 1 + strlen(e->d_name) >= PATH_MAX_LEN) {
            continue;   // 경로가 너무 김
        }
        path[base] = '/';
        strcpy(path + base + 1, e->d_name);
        if (is_dir) {
            scan_recursive(path, depth + 1);
        } else {
            char *copy = heap_caps_malloc(strlen(path) + 1, MALLOC_CAP_SPIRAM);
            if (copy) {
                strcpy(copy, path);
                s_files[s_count++] = copy;
            }
        }
        path[base] = '\0';
    }
    closedir(d);
}

static int scan_dir(const char *dir)
{
    char *path = malloc(PATH_MAX_LEN);
    if (!path) {
        return 0;
    }
    strlcpy(path, dir, PATH_MAX_LEN);
    scan_recursive(path, 0);
    free(path);
    // 전체 경로 기준 정렬 → 같은 폴더의 사진끼리 이어서 표시된다
    qsort(s_files, s_count, sizeof(char *), cmp_names);
    return s_count;
}

static void rescan(void)
{
    free_list();
    if (board_sdcard_is_mounted() && scan_dir(s_dir) > 0) {
        ESP_LOGI(TAG, "%d photos in %s", s_count, s_dir);
        return;
    }
    if (scan_dir(STORAGE_LFS_MOUNT_POINT "/photos") > 0) {
        ESP_LOGI(TAG, "%d photos in %s/photos", s_count, STORAGE_LFS_MOUNT_POINT);
        return;
    }
    ESP_LOGW(TAG, "no photos (%s%s)", s_dir, board_sdcard_is_mounted() ? "" : ": SD card not mounted");
}

// ---------------------------------------------------------------------------
// 크기 맞추기: 원본(방향 반영) → 표시 영역에 비율 유지로 맞춤, 확대는 하지 않음
// ---------------------------------------------------------------------------

static void fit(int ow, int oh, int *dw, int *dh)
{
    if (ow <= s_max_w && oh <= s_max_h) {
        *dw = ow;
        *dh = oh;
    } else if ((int64_t)ow * s_max_h > (int64_t)oh * s_max_w) {
        *dw = s_max_w;
        *dh = (int)((int64_t)oh * s_max_w / ow);
    } else {
        *dh = s_max_h;
        *dw = (int)((int64_t)ow * s_max_h / oh);
    }
    *dw = *dw < 1 ? 1 : *dw;
    *dh = *dh < 1 ? 1 : *dh;
}

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// ---------------------------------------------------------------------------
// JPEG 헤더 검사: EXIF 방향(APP1) 과 프레임 형식(SOFn)
//   방향: 1=정상, 3=180도, 6=시계 90도, 8=반시계 90도. 좌우 반전(2,4,5,7)은 무시.
//   ROM TJpgDec 는 baseline(SOF0), 컬러 3채널, Y 샘플링 1x1/2x1/2x2 만 지원한다.
// ---------------------------------------------------------------------------

static uint16_t rd16(const uint8_t *p, bool le) { return le ? p[0] | p[1] << 8 : p[0] << 8 | p[1]; }
static uint32_t rd32(const uint8_t *p, bool le)
{
    return le ? p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24
              : (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
}

typedef struct {
    int         orient;
    const char *unsupported;   // NULL 이면 디코딩 가능
} jpeg_probe_t;

static int exif_orientation(const uint8_t *seg, int len)
{
    if (len < 14 || memcmp(seg, "Exif\0\0", 6) != 0) {
        return 1;
    }
    const uint8_t *tiff = seg + 6;
    int tlen = len - 6;
    bool le = tiff[0] == 'I';
    uint32_t ifd = rd32(tiff + 4, le);
    if (ifd + 2 > (uint32_t)tlen) {
        return 1;
    }
    int n = rd16(tiff + ifd, le);
    for (int i = 0; i < n && ifd + 2 + (i + 1) * 12 <= (uint32_t)tlen; i++) {
        const uint8_t *entry = tiff + ifd + 2 + i * 12;
        if (rd16(entry, le) == 0x0112) {
            int o = rd16(entry + 8, le);
            return (o == 3 || o == 6 || o == 8) ? o : 1;
        }
    }
    return 1;
}

static const char *check_sof(uint8_t marker, const uint8_t *sof, int len)
{
    if (marker == 0xC2 || marker == 0xC6 || marker == 0xCA || marker == 0xCE) {
        return "progressive JPEG";
    }
    if (marker != 0xC0) {
        return "non-baseline JPEG (extended/lossless/arithmetic)";
    }
    if (len < 6 || sof[0] != 8) {
        return "not 8-bit JPEG";
    }
    int ncomp = sof[5];
    if (ncomp != 3 || len < 6 + 3 * ncomp) {
        return ncomp == 1 ? "grayscale JPEG" : "CMYK/unusual JPEG";
    }
    uint8_t y = sof[7];
    if (y != 0x11 && y != 0x21 && y != 0x22) {
        return "unsupported chroma subsampling";
    }
    if (sof[10] != 0x11 || sof[13] != 0x11) {
        return "unsupported chroma subsampling";
    }
    return NULL;
}

static jpeg_probe_t jpeg_probe(FILE *f)
{
    jpeg_probe_t r = { .orient = 1, .unsupported = "no SOF marker" };
    uint8_t hdr[4];
    if (fread(hdr, 1, 2, f) != 2 || hdr[0] != 0xFF || hdr[1] != 0xD8) {
        r.unsupported = "not a JPEG file";
        return r;
    }
    while (fread(hdr, 1, 4, f) == 4 && hdr[0] == 0xFF) {
        uint8_t marker = hdr[1];
        int len = (hdr[2] << 8 | hdr[3]) - 2;
        if (marker == 0xDA || len < 0) {
            break;   // 영상 데이터 시작
        }
        bool is_sof = marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC;
        bool is_exif = marker == 0xE1 && len >= 14 && len <= EXIF_MAX_SEGMENT;
        if (!is_sof && !is_exif) {
            fseek(f, len, SEEK_CUR);
            continue;
        }
        uint8_t *seg = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
        if (!seg || fread(seg, 1, len, f) != (size_t)len) {
            free(seg);
            r.unsupported = "truncated file";
            break;
        }
        if (is_exif) {
            r.orient = exif_orientation(seg, len);
        } else {
            r.unsupported = check_sof(marker, seg, len);
            free(seg);
            break;   // SOF 이후에는 필요한 정보 없음
        }
        free(seg);
    }
    return r;
}

// ---------------------------------------------------------------------------
// JPEG (ROM TJpgDec): 입력은 파일에서 512B 씩, 출력은 MCU 블록(RGB888) 단위
// 블록의 각 원본 픽셀을 방향 변환 후 목적지 좌표로 보낸다 (축소만 하므로 빈 픽셀 없음).
// ---------------------------------------------------------------------------

typedef struct {
    FILE     *f;
    uint16_t *dst;
    int       dw, dh;       // 목적지 크기
    int       sw, sh;       // 디코딩된(축소된) 원본 크기
    int       ow, oh;       // 방향 반영 원본 크기
    int       orient;
} jpg_ctx_t;

static UINT jpg_in(JDEC *jd, BYTE *buf, UINT len)
{
    jpg_ctx_t *c = jd->device;
    if (buf) {
        return (UINT)fread(buf, 1, len, c->f);
    }
    return fseek(c->f, len, SEEK_CUR) == 0 ? len : 0;
}

static UINT jpg_out(JDEC *jd, void *bitmap, JRECT *r)
{
    jpg_ctx_t *c = jd->device;
    const uint8_t *px = bitmap;
    for (int y = r->top; y <= r->bottom; y++) {
        for (int x = r->left; x <= r->right; x++, px += 3) {
            if (x >= c->sw || y >= c->sh) {
                continue;
            }
            int ox, oy;
            switch (c->orient) {
            case 3:  ox = c->sw - 1 - x; oy = c->sh - 1 - y; break;
            case 6:  ox = c->sh - 1 - y; oy = x;             break;
            case 8:  ox = y;             oy = c->sw - 1 - x; break;
            default: ox = x;             oy = y;             break;
            }
            int dx = ox * c->dw / c->ow;
            int dy = oy * c->dh / c->oh;
            c->dst[dy * c->dw + dx] = rgb565(px[0], px[1], px[2]);
        }
    }
    return 1;
}

static esp_err_t decode_jpeg(FILE *f, const char *path, photo_frame_t *frame)
{
    jpg_ctx_t c = { .f = f, .dst = frame->pixels };
    jpeg_probe_t probe = jpeg_probe(f);
    if (probe.unsupported) {
        ESP_LOGW(TAG, "skip %s: %s", path, probe.unsupported);
        return ESP_ERR_NOT_SUPPORTED;
    }
    c.orient = probe.orient;
    fseek(f, 0, SEEK_SET);

    void *pool = malloc(TJPGD_POOL_SIZE);
    ESP_RETURN_ON_FALSE(pool, ESP_ERR_NO_MEM, TAG, "tjpgd pool");
    JDEC jd;
    JRESULT res = jd_prepare(&jd, jpg_in, pool, TJPGD_POOL_SIZE, &c);
    if (res != JDR_OK) {
        free(pool);
        ESP_LOGW(TAG, "jpeg header error %d", res);
        return ESP_FAIL;
    }

    bool swap = c.orient == 6 || c.orient == 8;
    int full_ow = swap ? (int)jd.height : (int)jd.width;
    int full_oh = swap ? (int)jd.width : (int)jd.height;
    fit(full_ow, full_oh, &c.dw, &c.dh);

    // 목적지보다 작아지지 않는 범위에서 가장 많이 줄여서 디코딩 (1, 1/2, 1/4, 1/8)
    int scale = 0;
    while (scale < 3 && (full_ow >> (scale + 1)) >= c.dw && (full_oh >> (scale + 1)) >= c.dh) {
        scale++;
    }
    c.sw = (int)(jd.width >> scale);
    c.sh = (int)(jd.height >> scale);
    c.ow = swap ? c.sh : c.sw;
    c.oh = swap ? c.sw : c.sh;
    if (c.dw > c.ow) c.dw = c.ow;
    if (c.dh > c.oh) c.dh = c.oh;

    res = jd_decomp(&jd, jpg_out, (BYTE)scale);
    free(pool);
    if (res != JDR_OK) {
        ESP_LOGW(TAG, "jpeg decode error %d", res);
        return ESP_FAIL;
    }
    frame->width = c.dw;
    frame->height = c.dh;
    ESP_LOGI(TAG, "%ux%u (1/%d, exif %d) -> %dx%d", jd.width, jd.height, 1 << scale, c.orient, c.dw, c.dh);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// PNG (lodepng, RGB 24bit 로 전체 디코딩 후 축소)
// ---------------------------------------------------------------------------

static esp_err_t decode_png(FILE *f, const char *path, photo_frame_t *frame)
{
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > PNG_MAX_FILE) {
        ESP_LOGW(TAG, "skip %s: png file %ld KB too large (max %d KB)", path, size / 1024, PNG_MAX_FILE / 1024);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t *in = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(in, ESP_ERR_NO_MEM, TAG, "png file buffer");
    if (fread(in, 1, size, f) != (size_t)size) {
        free(in);
        return ESP_FAIL;
    }

    // IHDR 에서 크기만 먼저 확인 (바이트 16..23)
    uint32_t w = size > 24 ? rd32(in + 16, false) : 0;
    uint32_t h = size > 24 ? rd32(in + 20, false) : 0;
    if (w == 0 || h == 0 || (uint64_t)w * h > PNG_MAX_PIXELS) {
        ESP_LOGW(TAG, "skip %s: png %lux%lu too large (max %d px)", path,
                 (unsigned long)w, (unsigned long)h, PNG_MAX_PIXELS);
        free(in);
        return ESP_ERR_INVALID_SIZE;
    }

    // LVGL 의 lodepng 는 수정된 버전이다: 결과로 raw 픽셀이 아니라 lv_draw_buf_t* (RGBA 4B/px) 를
    // 돌려주고, 색 변환이 필요하면 같은 크기 버퍼를 하나 더 만든다.
    // 필요 메모리 ~= 압축 해제 원본(최대 4B/px) + draw buf 2개(8B/px), 파일은 이미 읽어 둠.
    size_t px = (size_t)w * h;
    size_t need = px * 4 + h + px * 8 + 64 * 1024;
    size_t free_now = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    if (need > free_now || px * 4 + h > largest) {
        ESP_LOGW(TAG, "skip %s: png %lux%lu needs ~%u KB, PSRAM free %u KB (largest %u KB)", path,
                 (unsigned long)w, (unsigned long)h, (unsigned)(need / 1024),
                 (unsigned)(free_now / 1024), (unsigned)(largest / 1024));
        free(in);
        return ESP_ERR_NO_MEM;
    }

    unsigned char *out = NULL;
    unsigned uw = 0, uh = 0;
    unsigned err = lodepng_decode32(&out, &uw, &uh, in, size);
    free(in);
    lv_draw_buf_t *buf = (lv_draw_buf_t *)out;
    if (err || !buf) {
        ESP_LOGW(TAG, "skip %s: png decode error %u (%s)", path, err, lodepng_error_text(err));
        if (buf) {
            lv_draw_buf_destroy(buf);
        }
        return ESP_FAIL;
    }

    const uint8_t *data = buf->data;
    uint32_t stride = buf->header.stride ? buf->header.stride : uw * 4;
    int dw, dh;
    fit((int)uw, (int)uh, &dw, &dh);
    for (int y = 0; y < dh; y++) {
        const uint8_t *row = data + (size_t)(y * (int)uh / dh) * stride;
        for (int x = 0; x < dw; x++) {
            const uint8_t *p = row + (x * (int)uw / dw) * 4;   // R, G, B, A
            // 투명 부분은 검은 배경과 합성
            uint8_t a = p[3];
            frame->pixels[y * dw + x] = rgb565(p[0] * a / 255, p[1] * a / 255, p[2] * a / 255);
        }
    }
    lv_draw_buf_destroy(buf);
    frame->width = dw;
    frame->height = dh;
    ESP_LOGI(TAG, "png %ux%u -> %dx%d", uw, uh, dw, dh);
    return ESP_OK;
}

static esp_err_t decode_file(const char *path, photo_frame_t *frame)
{
    FILE *f = fopen(path, "rb");
    ESP_RETURN_ON_FALSE(f, ESP_ERR_NOT_FOUND, TAG, "open %s", path);
    setvbuf(f, NULL, _IOFBF, FILE_BUF_SIZE);

    int64_t t0 = esp_timer_get_time();
    esp_err_t err = has_ext(path, "png") ? decode_png(f, path, frame) : decode_jpeg(f, path, frame);
    fclose(f);
    if (err == ESP_OK) {
        strlcpy(frame->path, path, sizeof(frame->path));
        ESP_LOGI(TAG, "%s decoded in %d ms", path, (int)((esp_timer_get_time() - t0) / 1000));
    }
    return err;
}

// ---------------------------------------------------------------------------
// 태스크
// ---------------------------------------------------------------------------

static void publish(const photo_frame_t *frame)
{
    app_event_post(APP_EVT_PHOTO_READY, &frame, sizeof(frame));
}

static void on_app_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    uint32_t bit = id == APP_EVT_REQ_PHOTO_NEXT ? NOTIFY_NEXT
                 : id == APP_EVT_REQ_PHOTO_PREV ? NOTIFY_PREV
                 : id == APP_EVT_SETTINGS_CHANGED ? NOTIFY_SETTINGS
                 : id == APP_EVT_REQ_PHOTO_RESCAN ? NOTIFY_RESCAN : 0;
    if (bit && s_task) {
        xTaskNotify(s_task, bit, eSetBits);
    }
}

static void reload_interval(void)
{
    app_settings_t *s = malloc(sizeof(app_settings_t));
    if (s && settings_load(s) == ESP_OK) {
        s_interval_s = s->photo_interval_s;
        ESP_LOGI(TAG, "interval %d s", s_interval_s);
    }
    free(s);
}

// 목록을 다시 읽고, 표시 중인 사진(current)의 새 목록 번호를 반환한다 (없어졌으면 가까운 번호).
static int rescan_keep(const char *current, int index)
{
    rescan();
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_files[i], current) == 0) {
            return i;
        }
    }
    return index < s_count ? index : s_count - 1;
}

// 간격만큼 기다린 뒤 다음(+1)/이전(-1) 을 반환. 탭 요청은 바로, 설정 변경과 목록 갱신은 반영 후 계속 대기.
// 목록이 바뀌면 *index 를 새 목록 기준으로 고친다.
static int wait_for_step(const char *current, int *index)
{
    for (;;) {
        uint32_t bits = 0;
        if (xTaskNotifyWait(0, UINT32_MAX, &bits, pdMS_TO_TICKS(s_interval_s * 1000)) == pdFALSE) {
            return 1;
        }
        if (bits & NOTIFY_SETTINGS) {
            reload_interval();
        }
        if (bits & NOTIFY_RESCAN) {
            *index = rescan_keep(current, *index);
            if (s_count == 0) {
                return 1;   // 사진이 모두 삭제됨: 태스크 루프가 빈 화면을 처리
            }
        }
        if (bits & NOTIFY_PREV) {
            return -1;
        }
        if (bits & NOTIFY_NEXT) {
            return 1;
        }
    }
}

static void photo_task(void *arg)
{
    int front = -1;       // 화면에 표시 중인 프레임 (0/1)
    int index = -1;       // 표시 중인 사진의 목록 번호
    int step = 1;         // 다음에 보여줄 방향

    rescan();
    for (;;) {
        if (s_count == 0) {
            publish(NULL);
            xTaskNotifyWait(0, UINT32_MAX, NULL, pdMS_TO_TICKS(EMPTY_RESCAN_MS));
            rescan();
            index = -1;
            continue;
        }

        // 다음 사진을 뒤쪽 버퍼에 디코딩 (실패한 파일은 건너뜀, 한 바퀴 돌면 목록 갱신)
        int back = front == 0 ? 1 : 0;
        bool shown = false;
        for (int tries = 0; tries < s_count && !shown; tries++) {
            int next = index + step;
            if (next >= s_count) {
                rescan();
                if (s_count == 0) {
                    break;
                }
                next = 0;
            } else if (next < 0) {
                next = s_count - 1;
            }
            index = next;
            if (decode_file(s_files[index], &s_frames[back]) == ESP_OK) {
                front = back;
                publish(&s_frames[front]);
                shown = true;
            }
        }
        if (!shown) {
            // 표시할 수 있는 사진이 하나도 없음: 계속 돌지 말고 기다렸다가 목록을 다시 읽는다
            ESP_LOGW(TAG, "no decodable photos, retry in %d s", EMPTY_RESCAN_MS / 1000);
            publish(NULL);
            xTaskNotifyWait(0, UINT32_MAX, NULL, pdMS_TO_TICKS(EMPTY_RESCAN_MS));
            rescan();
            index = -1;
            continue;
        }

        step = wait_for_step(s_frames[front].path, &index);
    }
}

esp_err_t photo_start(const char *dir, int interval_s, uint16_t max_w, uint16_t max_h)
{
    strlcpy(s_dir, dir, sizeof(s_dir));
    s_interval_s = interval_s > 0 ? interval_s : 10;
    s_max_w = max_w;
    s_max_h = max_h;

    s_files = heap_caps_calloc(MAX_FILES, sizeof(char *), MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(s_files, ESP_ERR_NO_MEM, TAG, "file list");
    for (int i = 0; i < 2; i++) {
        s_frames[i].pixels = heap_caps_malloc(FRAME_PIXELS * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
        ESP_RETURN_ON_FALSE(s_frames[i].pixels, ESP_ERR_NO_MEM, TAG, "frame buffer");
    }
    ESP_RETURN_ON_ERROR(esp_event_handler_register(APP_EVENT, ESP_EVENT_ANY_ID, on_app_event, NULL), TAG, "event");

    // core 1 (LVGL 과 같은 코어) 에서 LVGL 보다 낮은 우선순위로 → 렌더링을 방해하지 않음
    BaseType_t ok = xTaskCreatePinnedToCore(photo_task, "photo", 6 * 1024, NULL, 2, &s_task, 1);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "task");
    ESP_LOGI(TAG, "slideshow every %d s, area %ux%u", s_interval_s, max_w, max_h);
    return ESP_OK;
}
