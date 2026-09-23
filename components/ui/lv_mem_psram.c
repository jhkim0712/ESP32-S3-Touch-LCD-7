// LVGL 메모리 할당자 (CONFIG_LV_USE_CUSTOM_MALLOC): 모든 LVGL 할당을 PSRAM 으로 보낸다.
//
// C 라이브러리 malloc 을 쓰면 ESP-IDF 설정(SPIRAM_MALLOC_ALWAYSINTERNAL=4096)에 따라
// 4KB 미만 할당은 내부 RAM 으로 간다. LVGL 객체와 한글 글리프 캐시는 대부분 작은 할당이라
// 내부 RAM 을 수십 KB 차지하게 되고, 이는 Wi-Fi/TLS/DMA 가 써야 할 공간이다.
// PSRAM 이 부족할 때만 내부 RAM 을 사용한다.

#include "lvgl.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#include "esp_heap_caps.h"

#define LV_HEAP_CAPS    (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

void lv_mem_init(void)
{
}

void lv_mem_deinit(void)
{
}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;   // 지원 안 함
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
}

void *lv_malloc_core(size_t size)
{
    void *p = heap_caps_malloc(size, LV_HEAP_CAPS);
    return p ? p : heap_caps_malloc(size, MALLOC_CAP_8BIT);
}

void *lv_realloc_core(void *p, size_t new_size)
{
    void *n = heap_caps_realloc(p, new_size, LV_HEAP_CAPS);
    return n ? n : heap_caps_realloc(p, new_size, MALLOC_CAP_8BIT);
}

void lv_free_core(void *p)
{
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p)
{
    multi_heap_info_t info;
    heap_caps_get_info(&info, LV_HEAP_CAPS);
    mon_p->total_size = info.total_free_bytes + info.total_allocated_bytes;
    mon_p->free_size = info.total_free_bytes;
    mon_p->free_biggest_size = info.largest_free_block;
    mon_p->used_cnt = info.allocated_blocks;
    mon_p->free_cnt = info.free_blocks;
    mon_p->used_pct = mon_p->total_size ? 100 - (uint8_t)(100 * mon_p->free_size / mon_p->total_size) : 0;
    mon_p->frag_pct = 0;
}

lv_result_t lv_mem_test_core(void)
{
    return heap_caps_check_integrity(LV_HEAP_CAPS, false) ? LV_RESULT_OK : LV_RESULT_INVALID;
}

#endif
