/* Host shim for <esp_lvgl_port.h> with a controllable, non-reentrant lock. */
#ifndef HOST_ESP_LVGL_PORT_H
#define HOST_ESP_LVGL_PORT_H

#include <esp_err.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int task_priority;
    int task_affinity;
    int task_stack;
    int timer_period_ms;
} lvgl_port_cfg_t;

#define ESP_LVGL_PORT_INIT_CONFIG() \
    {                               \
        .task_priority = 4,         \
        .task_affinity = -1,        \
        .task_stack = 8192,         \
        .timer_period_ms = 33,      \
    }

typedef struct {
    esp_lcd_panel_io_handle_t io_handle;
    esp_lcd_panel_handle_t panel_handle;
    void* control_handle;
    uint32_t buffer_size;
    bool double_buffer;
    uint32_t trans_size;
    uint32_t hres;
    uint32_t vres;
    bool monochrome;
    struct {
        bool swap_xy;
        bool mirror_x;
        bool mirror_y;
    } rotation;
    lv_color_format_t color_format;
    struct {
        bool buff_dma;
        bool buff_spiram;
        bool sw_rotate;
        bool swap_bytes;
        bool full_refresh;
        bool direct_mode;
    } flags;
} lvgl_port_display_cfg_t;

typedef struct {
    struct {
        bool bb_mode;
        bool avoid_tearing;
    } flags;
} lvgl_port_display_rgb_cfg_t;

typedef struct {
    struct {
        bool avoid_tearing;
    } flags;
} lvgl_port_display_dsi_cfg_t;

esp_err_t lvgl_port_init(const lvgl_port_cfg_t* cfg);
lv_display_t* lvgl_port_add_disp(const lvgl_port_display_cfg_t* cfg);
lv_display_t* lvgl_port_add_disp_rgb(const lvgl_port_display_cfg_t* cfg,
                                     const lvgl_port_display_rgb_cfg_t* rgb_cfg);
lv_display_t* lvgl_port_add_disp_dsi(const lvgl_port_display_cfg_t* cfg,
                                     const lvgl_port_display_dsi_cfg_t* dsi_cfg);
bool lvgl_port_lock(uint32_t timeout_ms);
void lvgl_port_unlock(void);

/* Host control: make the next n lock attempts fail. */
void host_lvgl_port_fail_next_locks(size_t n);

#ifdef __cplusplus
}
#endif

#endif /* HOST_ESP_LVGL_PORT_H */
