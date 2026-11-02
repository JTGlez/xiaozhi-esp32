/* esp_timer.h host implementation: registry of real production callbacks. */
#include "esp_timer.h"

#include <stdlib.h>
#include <string.h>

#define MAX_TIMERS 32
#define HOST_ESP_ERR_INVALID_STATE 0x103

struct host_esp_timer {
    bool used;
    bool alive;
    bool pending;
    esp_timer_create_args_t args;
};

static struct host_esp_timer g_timers[MAX_TIMERS];
static uint64_t g_clock_us = 0;

static esp_err_t esp_err_invalid(void) { return ESP_ERR_INVALID_ARG; }

esp_err_t esp_timer_create(const esp_timer_create_args_t* args, esp_timer_handle_t* ret) {
    if (args == NULL || ret == NULL)
        return esp_err_invalid();
    for (int i = 0; i < MAX_TIMERS; ++i) {
        if (!g_timers[i].used) {
            struct host_esp_timer* t = &g_timers[i];
            t->used = true;
            t->alive = true;
            t->pending = false;
            t->args = *args;
            *ret = t;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t esp_timer_start_once(esp_timer_handle_t timer, uint64_t period_us) {
    (void)period_us;
    if (timer == NULL || !timer->used || !timer->alive)
        return esp_err_invalid();
    timer->pending = true;
    return ESP_OK;
}

esp_err_t esp_timer_start_periodic(esp_timer_handle_t timer, uint64_t period_us) {
    return esp_timer_start_once(timer, period_us);
}

esp_err_t esp_timer_stop(esp_timer_handle_t timer) {
    if (timer == NULL || !timer->used)
        return esp_err_invalid();
    timer->pending = false;
    return ESP_OK;
}

esp_err_t esp_timer_delete(esp_timer_handle_t timer) {
    if (timer == NULL)
        return esp_err_invalid();
    if (!timer->used)
        return HOST_ESP_ERR_INVALID_STATE;
    timer->pending = false;
    timer->alive = false;
    timer->used = false;
    return ESP_OK;
}

int64_t esp_timer_get_time(void) { return (int64_t)g_clock_us; }

void host_set_clock_ms(uint64_t ms) { g_clock_us = ms * 1000ULL; }
uint64_t host_clock_ms(void) { return g_clock_us / 1000ULL; }

int host_esp_timer_pending(const char* name) {
    int n = 0;
    for (int i = 0; i < MAX_TIMERS; ++i) {
        if (g_timers[i].used && g_timers[i].alive && g_timers[i].pending &&
            strcmp(g_timers[i].args.name, name) == 0) {
            ++n;
        }
    }
    return n;
}

int host_esp_timer_live_count(const char* name) {
    int n = 0;
    for (int i = 0; i < MAX_TIMERS; ++i) {
        if (g_timers[i].used && g_timers[i].alive && strcmp(g_timers[i].args.name, name) == 0) {
            ++n;
        }
    }
    return n;
}

int host_esp_timer_fire(const char* name) {
    int fired = 0;
    for (int i = 0; i < MAX_TIMERS; ++i) {
        struct host_esp_timer* t = &g_timers[i];
        if (t->used && t->alive && t->pending && strcmp(t->args.name, name) == 0) {
            t->pending = false; /* one-shot semantics */
            if (t->args.callback != NULL) {
                t->args.callback(t->args.arg);
            }
            ++fired;
        }
    }
    return fired;
}
