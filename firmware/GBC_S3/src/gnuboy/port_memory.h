#ifndef GNUBOY_PORT_MEMORY_H
#define GNUBOY_PORT_MEMORY_H
#include <stdlib.h>
#include <stddef.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_heap_caps.h>
static inline void *gnuboy_psram_malloc(size_t n) {
  return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static inline void *gnuboy_psram_realloc(void *p, size_t n) {
  return heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
static inline void gnuboy_psram_free(void *p) { if (p) heap_caps_free(p); }
#else
static inline void *gnuboy_psram_malloc(size_t n) { return malloc(n); }
static inline void *gnuboy_psram_realloc(void *p, size_t n) { return realloc(p, n); }
static inline void gnuboy_psram_free(void *p) { free(p); }
#endif
#endif
