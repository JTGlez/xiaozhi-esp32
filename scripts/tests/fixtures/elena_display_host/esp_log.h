/* Host shim for <esp_log.h>. Diagnostics go to stderr. */
#ifndef HOST_ESP_LOG_H
#define HOST_ESP_LOG_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HOST_ESP_LOG(level, tag, ...)            \
    do {                                         \
        fprintf(stderr, "[%s/%s] ", level, tag); \
        fprintf(stderr, __VA_ARGS__);            \
        fprintf(stderr, "\n");                   \
    } while (0)

#define ESP_LOGE(tag, ...) HOST_ESP_LOG("E", tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) HOST_ESP_LOG("W", tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) HOST_ESP_LOG("I", tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) HOST_ESP_LOG("D", tag, __VA_ARGS__)
#define ESP_LOGV(tag, ...) HOST_ESP_LOG("V", tag, __VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif /* HOST_ESP_LOG_H */
