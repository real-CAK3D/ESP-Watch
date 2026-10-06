// LVGL custom allocator: widgets, styles and labels go to PSRAM so the scarce internal
// RAM stays free for NimBLE, WiFi-less radio buffers and the LVGL draw buffers.
#include <lvgl.h>
#include <esp_heap_caps.h>

void lv_mem_init(void) {}
void lv_mem_deinit(void) {}
lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes) {
  (void)mem;
  (void)bytes;
  return NULL;
}
void lv_mem_remove_pool(lv_mem_pool_t pool) { (void)pool; }

void *lv_malloc_core(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
void *lv_realloc_core(void *p, size_t new_size) {
  return heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
void lv_free_core(void *p) { heap_caps_free(p); }

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p) { (void)mon_p; }
lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }
