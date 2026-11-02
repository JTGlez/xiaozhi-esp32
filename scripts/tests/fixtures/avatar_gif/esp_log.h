/* Host shim for esp_log.h. Diagnostics go to stderr so a test can require that
 * the decoder stayed quiet. */
#ifndef HOST_ESP_LOG_SHIM_H
#define HOST_ESP_LOG_SHIM_H

#include <stdio.h>

#define ESP_LOGE(tag, ...)                 \
    do {                                   \
        fprintf(stderr, "[E/%s] ", (tag)); \
        fprintf(stderr, __VA_ARGS__);      \
        fprintf(stderr, "\n");             \
    } while (0)

#define ESP_LOGW(tag, ...)                 \
    do {                                   \
        fprintf(stderr, "[W/%s] ", (tag)); \
        fprintf(stderr, __VA_ARGS__);      \
        fprintf(stderr, "\n");             \
    } while (0)

#define ESP_LOGI(tag, ...)                 \
    do {                                   \
        fprintf(stderr, "[I/%s] ", (tag)); \
        fprintf(stderr, __VA_ARGS__);      \
        fprintf(stderr, "\n");             \
    } while (0)

#define ESP_LOGD(tag, ...)                 \
    do {                                   \
        fprintf(stderr, "[D/%s] ", (tag)); \
        fprintf(stderr, __VA_ARGS__);      \
        fprintf(stderr, "\n");             \
    } while (0)

#endif /* HOST_ESP_LOG_SHIM_H */
