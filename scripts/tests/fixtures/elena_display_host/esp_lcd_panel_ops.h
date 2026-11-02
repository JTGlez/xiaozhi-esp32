/* Host shim for <esp_lcd_panel_ops.h>. */
#ifndef HOST_ESP_LCD_PANEL_OPS_H
#define HOST_ESP_LCD_PANEL_OPS_H

#include <esp_err.h>
#include <esp_lcd_panel_io.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _esp_lcd_panel_t* esp_lcd_panel_handle_t;

esp_err_t esp_lcd_panel_draw_bitmap(esp_lcd_panel_handle_t panel, int x_start, int y_start,
                                    int x_end, int y_end, const void* data);
esp_err_t esp_lcd_panel_disp_on_off(esp_lcd_panel_handle_t panel, bool on_off);
esp_err_t esp_lcd_panel_del(esp_lcd_panel_handle_t panel);

#ifdef __cplusplus
}
#endif

#endif /* HOST_ESP_LCD_PANEL_OPS_H */
