/* esp_lvgl_port.h host implementation: non-reentrant lock with injection. */
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "lvgl.h"

extern int g_lvgl_host_locked;
void host_lvgl_log_event(int kind, const void* obj, uintptr_t arg);
lv_display_t* host_lvgl_create_display(uint32_t w, uint32_t h);
#define HOST_LOCK_REJECTED 14 /* HOST_LVGL_EV_LOCK_REJECTED */

static size_t g_fail_locks = 0;

void host_lvgl_port_fail_next_locks(size_t n) { g_fail_locks = n; }

esp_err_t lvgl_port_init(const lvgl_port_cfg_t* cfg) {
    (void)cfg;
    return ESP_OK;
}

static lv_display_t* add_disp_common(const lvgl_port_display_cfg_t* cfg) {
    if (cfg == NULL)
        return NULL;
    return host_lvgl_create_display(cfg->hres, cfg->vres);
}

lv_display_t* lvgl_port_add_disp(const lvgl_port_display_cfg_t* cfg) {
    return add_disp_common(cfg);
}
lv_display_t* lvgl_port_add_disp_rgb(const lvgl_port_display_cfg_t* cfg,
                                     const lvgl_port_display_rgb_cfg_t* rgb_cfg) {
    (void)rgb_cfg;
    return add_disp_common(cfg);
}
lv_display_t* lvgl_port_add_disp_dsi(const lvgl_port_display_cfg_t* cfg,
                                     const lvgl_port_display_dsi_cfg_t* dsi_cfg) {
    (void)dsi_cfg;
    return add_disp_common(cfg);
}

bool lvgl_port_lock(uint32_t timeout_ms) {
    (void)timeout_ms;
    if (g_fail_locks > 0) {
        --g_fail_locks;
        host_lvgl_log_event(HOST_LOCK_REJECTED, NULL, 0);
        return false;
    }
    ++g_lvgl_host_locked;
    return true;
}

void lvgl_port_unlock(void) { --g_lvgl_host_locked; }
