/* Host shim for <esp_timer.h>.
 *
 * Callbacks are never fired by the wall clock here: the harness fires the
 * REAL callbacks registered by the production code through
 * host_esp_timer_fire(name), so tests exercise the exact callback the
 * production code installed.
 */
#ifndef HOST_ESP_TIMER_H
#define HOST_ESP_TIMER_H

#include <esp_err.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*esp_timer_cb_t)(void* arg);

typedef enum {
    ESP_TIMER_TASK = 0,
    ESP_TIMER_ANY = 1,
} esp_timer_dispatch_t;

typedef struct {
    esp_timer_cb_t callback;
    void* arg;
    esp_timer_dispatch_t dispatch_method;
    const char* name;
    bool skip_unhandled_events;
} esp_timer_create_args_t;

typedef struct host_esp_timer* esp_timer_handle_t;

esp_err_t esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* ret_handle);
esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t period_us);
esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period_us);
esp_err_t esp_timer_stop(esp_timer_handle_t timer);
esp_err_t esp_timer_delete(esp_timer_handle_t timer);
int64_t esp_timer_get_time(void);

/* Host controls */
int host_esp_timer_pending(const char* name);
int host_esp_timer_fire(const char* name);
int host_esp_timer_live_count(const char* name);
void host_set_clock_ms(uint64_t ms);
uint64_t host_clock_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* HOST_ESP_TIMER_H */
