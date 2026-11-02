/* Host shim for <esp_heap_caps.h>: plain malloc/free behind the API. */
#ifndef HOST_ESP_HEAP_CAPS_H
#define HOST_ESP_HEAP_CAPS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MALLOC_CAP_8BIT (1u << 0)
#define MALLOC_CAP_INTERNAL (1u << 1)
#define MALLOC_CAP_SPIRAM (1u << 2)

void* heap_caps_malloc(size_t size, uint32_t caps);
void* heap_caps_calloc(size_t n, size_t size, uint32_t caps);
void heap_caps_free(void* ptr);
size_t heap_caps_get_total_size(uint32_t caps);

#ifdef __cplusplus
}
#endif

#endif /* HOST_ESP_HEAP_CAPS_H */
