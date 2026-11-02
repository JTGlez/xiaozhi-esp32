/* Host shim for <esp_pm.h>. */
#ifndef HOST_ESP_PM_H
#define HOST_ESP_PM_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void* esp_pm_lock_handle_t;

typedef enum {
    ESP_PM_NONE = 0,
    ESP_PM_APB_FREQ_MAX = 1,
} esp_pm_lock_type_t;

esp_err_t esp_pm_lock_create(esp_pm_lock_type_t lock_type, int arg, const char* name,
                             esp_pm_lock_handle_t* handle);
void esp_pm_lock_delete(esp_pm_lock_handle_t handle);
esp_err_t esp_pm_lock_acquire(esp_pm_lock_handle_t handle);
esp_err_t esp_pm_lock_release(esp_pm_lock_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* HOST_ESP_PM_H */
