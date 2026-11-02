/* Host shim for <esp_system.h>. */
#ifndef HOST_ESP_SYSTEM_H
#define HOST_ESP_SYSTEM_H

#ifdef __cplusplus
extern "C" {
#endif

void esp_system_abort(const char* what);

#ifdef __cplusplus
}
#endif

#endif /* HOST_ESP_SYSTEM_H */
