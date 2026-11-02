/* Host shim for <esp_lcd_panel_io.h>. */
#ifndef HOST_ESP_LCD_PANEL_IO_H
#define HOST_ESP_LCD_PANEL_IO_H

#include <esp_err.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _esp_lcd_panel_io_t* esp_lcd_panel_io_handle_t;

esp_err_t esp_lcd_panel_io_del(esp_lcd_panel_io_handle_t io);

#ifdef __cplusplus
}
#endif

#endif /* HOST_ESP_LCD_PANEL_IO_H */
