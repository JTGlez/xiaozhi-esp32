/* Host shim for <esp_err.h>. */
#ifndef HOST_ESP_ERR_H
#define HOST_ESP_ERR_H

#include <stdio.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_NOT_FOUND 0x106
#define ESP_ERR_NOT_SUPPORTED 0x10C

#define ESP_ERROR_CHECK(x)                                                   \
    do {                                                                     \
        esp_err_t err_rc_ = (x);                                             \
        if (err_rc_ != ESP_OK) {                                             \
            fprintf(stderr, "ESP_ERROR_CHECK failed: 0x%x\n", (int)err_rc_); \
            abort();                                                         \
        }                                                                    \
    } while (0)

#ifdef __cplusplus
}
#endif

#endif /* HOST_ESP_ERR_H */
