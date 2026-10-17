#pragma once

#include <esp_heap_caps.h>
#include <stdlib.h>

// Allocates in PSRAM, falling back to internal RAM. Returns nullptr when both are exhausted.
template <typename T>
T *psramAlloc(size_t count) {
  void *p = heap_caps_malloc(count * sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return static_cast<T *>(p ? p : malloc(count * sizeof(T)));
}
