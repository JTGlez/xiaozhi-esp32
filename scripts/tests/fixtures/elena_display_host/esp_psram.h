/* Host shim for <esp_psram.h>. */
#ifndef HOST_ESP_PSRAM_H
#define HOST_ESP_PSRAM_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t esp_psram_get_size(void);

#ifdef __cplusplus
}
#endif

#endif /* HOST_ESP_PSRAM_H */
