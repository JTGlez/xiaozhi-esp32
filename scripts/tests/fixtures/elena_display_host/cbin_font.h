/* Host stub for <cbin_font.h>: the cbin loaders are not used by Elena. */
#ifndef HOST_CBIN_FONT_H
#define HOST_CBIN_FONT_H

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

lv_font_t* cbin_font_create(uint8_t* data);
void cbin_font_delete(lv_font_t* font);
lv_img_dsc_t* cbin_img_dsc_create(uint8_t* data);
void cbin_img_dsc_delete(lv_img_dsc_t* dsc);

#ifdef __cplusplus
}
#endif

#endif /* HOST_CBIN_FONT_H */
