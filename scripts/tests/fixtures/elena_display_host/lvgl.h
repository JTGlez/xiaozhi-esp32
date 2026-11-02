/* Host shim for <lvgl.h>.
 *
 * It models the LVGL surface the firmware display code actually uses: an
 * object tree with hidden flags, label text, image source/scale, a small set
 * of style properties, timers and the memory API gifdec.c calls. It exists so
 * the REAL display sources (display.cc, lvgl_display.cc, lcd_display.cc,
 * lvgl_image.cc, emoji_collection.cc, lvgl_gif.cc, gifdec.c) compile and run
 * unchanged on a workstation.
 *
 * Boundaries (documented, not asserted beyond them):
 *  - No rendering, layout engine, flex flow or animation: style/flex/anim
 *    setters are stored only; positions are computed for the alignment modes
 *    the display code uses (CENTER, TOP_MID, BOTTOM_MID, TOP_LEFT).
 *  - lv_malloc/lv_free are host malloc/free wrapped with a counter for the
 *    large workspace allocation gifdec.c performs per decoder open, plus an
 *    allocation-failure injection used to test the OOM path.
 *  - Every graphics mutation records a log entry tagged with whether the
 *    display lock was held at that moment.
 */
#ifndef HOST_LVGL_SHIM_H
#define HOST_LVGL_SHIM_H

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ config */
#define LV_USE_DRAW_SW_ASM 0
#define LV_DRAW_SW_ASM_HELIUM 1

/* ------------------------------------------------------------------ basic */
typedef uint8_t lv_opa_t;
typedef int32_t lv_coord_t;
typedef int lv_res_t;
typedef uint32_t lv_color_format_t;
#define LV_RESULT_OK 0
#define LV_RESULT_INVALID 1

#define LV_OPA_TRANSP 0
#define LV_OPA_50 127
#define LV_OPA_70 178
#define LV_OPA_COVER 255

#define LV_SIZE_CONTENT ((lv_coord_t)0x7FE00000)

typedef struct {
    uint32_t val; /* packed 0xRRGGBB */
} lv_color_t;

static inline lv_color_t lv_color_hex(uint32_t c) {
    lv_color_t color;
    color.val = c & 0xFFFFFFu;
    return color;
}
static inline lv_color_t lv_color_make(uint8_t r, uint8_t g, uint8_t b) {
    return lv_color_hex(((uint32_t)r << 16) | ((uint32_t)g << 8) | b);
}
static inline lv_color_t lv_color_black(void) { return lv_color_hex(0x000000); }
static inline lv_color_t lv_color_white(void) { return lv_color_hex(0xFFFFFF); }

/* ------------------------------------------------------------------- image */
#define LV_IMAGE_HEADER_MAGIC 0x19u
#define LV_IMAGE_FLAGS_MODIFIABLE (1u << 3)
#define LV_COLOR_FORMAT_RGB565 0x15u
#define LV_COLOR_FORMAT_ARGB8888 0x0Fu
#define LV_COLOR_FORMAT_RAW_ALPHA 0x03u

typedef struct {
    uint32_t magic;
    uint32_t flags;
    uint32_t cf;
    uint16_t w;
    uint16_t h;
    uint16_t stride;
} lv_image_header_t;

typedef struct {
    lv_image_header_t header;
    uint32_t data_size;
    const uint8_t* data;
} lv_image_dsc_t;
typedef lv_image_dsc_t lv_img_dsc_t;

/* Matches the vendored lv_draw_buf.h struct (_lv_draw_buf_t). */
typedef struct _lv_draw_buf_handlers_t lv_draw_buf_handlers_t;
struct _lv_draw_buf_t {
    lv_image_header_t header;
    uint32_t data_size;
    uint8_t* data;
    void* unaligned_data;
    const lv_draw_buf_handlers_t* handlers;
};
typedef struct _lv_draw_buf_t lv_draw_buf_t;

/* -------------------------------------------------------------------- font */
#define LV_FONT_SUBPX_NONE 0
#define LV_FONT_KERNING_NONE 0
#define LV_FONT_FMT_TXT_PLAIN 0
#define LV_FONT_FMT_TXT_CMAP_SPARSE_TINY 3

typedef struct _lv_font_fmt_txt_glyph_dsc_t {
    uint32_t bitmap_index;
    uint16_t adv_w;
    uint16_t box_w;
    uint16_t box_h;
    int16_t ofs_x;
    int16_t ofs_y;
    uint8_t r;
} lv_font_fmt_txt_glyph_dsc_t;

typedef struct _lv_font_fmt_txt_cmap_t {
    const uint16_t* unicode_list;
    const lv_font_fmt_txt_glyph_dsc_t* glyph_dsc;
    uint32_t range_start;
    uint16_t range_length;
    uint16_t glyph_id_start;
    const void* glyph_id_ofs_list;
    uint16_t list_length;
    uint8_t type;
} lv_font_fmt_txt_cmap_t;

typedef struct _lv_font_fmt_txt_dsc_t {
    const lv_font_fmt_txt_cmap_t* cmaps;
    const lv_font_fmt_txt_glyph_dsc_t* glyph_dsc;
    const uint8_t* glyph_bitmap;
    const void* kern_dsc;
    uint16_t cmap_num;
    uint16_t glyph_cnt;
    uint8_t bpp;
    uint8_t kern_classes;
    uint8_t bitmap_format;
    uint8_t kern_scale;
    uint8_t stride;
} lv_font_fmt_txt_dsc_t;

struct _lv_font_t;
typedef bool (*lv_font_get_glyph_dsc_fn)(const struct _lv_font_t* f, uint32_t letter,
                                         uint32_t letter_next, void* glyph_out);
typedef const void* (*lv_font_get_glyph_bitmap_fn)(const void* dsc, uint32_t glyph_index);

typedef struct _lv_font_t {
    const uint8_t* bitmap_data;
    const lv_font_fmt_txt_dsc_t* dsc;
    uint16_t line_height;
    uint16_t base_line;
    uint8_t subpx;
    uint8_t kerning;
    uint8_t static_bitmap;
    int8_t underline_position;
    int8_t underline_thickness;
    lv_font_get_glyph_dsc_fn get_glyph_dsc;
    lv_font_get_glyph_bitmap_fn get_glyph_bitmap;
    void (*release_glyph)(const struct _lv_font_t* f, void* glyph_bitmap);
    const struct _lv_font_t* fallback;
    void* user_data;
} lv_font_t;

#define LV_FONT_DECLARE(font_name) extern const lv_font_t font_name
#define LV_IMG_DECLARE(img_name) extern const lv_img_dsc_t img_name

bool lv_font_get_glyph_dsc_fmt_txt(const lv_font_t* font, uint32_t letter, uint32_t letter_next,
                                   void* glyph_out);
const void* lv_font_get_bitmap_fmt_txt(const void* letter_bitmap_dsc, uint32_t glyph_index);

/* ---------------------------------------------------------------- display */
typedef struct _lv_obj_t lv_obj_t;

typedef struct _lv_display_t {
    uint32_t hor_res;
    uint32_t ver_res;
    bool alive;
} lv_display_t;

void lv_init(void);
lv_display_t* lv_display_get_default(void);
/* Matches the repo's vendored LVGL: lv_display.h declares
 * `lv_obj_t * lv_screen_active(void);` — the active screen object. */
lv_obj_t* lv_screen_active(void);
uint32_t lv_display_get_horizontal_resolution(const lv_display_t* disp);
uint32_t lv_display_get_vertical_resolution(const lv_display_t* disp);
#define LV_HOR_RES lv_display_get_horizontal_resolution(lv_display_get_default())
#define LV_VER_RES lv_display_get_vertical_resolution(lv_display_get_default())
void lv_display_set_offset(lv_display_t* disp, lv_coord_t offsetx, lv_coord_t offsety);
void lv_display_delete(lv_display_t* disp);
lv_res_t lv_image_cache_resize(uint32_t size, bool user_cache);

/* ------------------------------------------------------------------- misc */
typedef enum { LV_PART_MAIN = 0 } lv_part_t;
#define LV_SCROLLBAR_MODE_OFF 0
#define LV_SCROLLBAR_MODE_AUTO 1
typedef enum { LV_LAYOUT_NONE = 0, LV_LAYOUT_FLEX = 1 } lv_layout_t;
typedef enum {
    LV_FLEX_FLOW_NONE = 0,
    LV_FLEX_FLOW_ROW = 1,
    LV_FLEX_FLOW_COLUMN = 2
} lv_flex_flow_t;
typedef enum {
    LV_FLEX_ALIGN_START = 0,
    LV_FLEX_ALIGN_END = 1,
    LV_FLEX_ALIGN_CENTER = 2,
    LV_FLEX_ALIGN_SPACE_BETWEEN = 3
} lv_flex_align_t;
typedef enum {
    LV_TEXT_ALIGN_LEFT = 0,
    LV_TEXT_ALIGN_CENTER = 1,
    LV_TEXT_ALIGN_RIGHT = 2
} lv_text_align_t;
typedef enum {
    LV_LABEL_LONG_WRAP = 0,
    LV_LABEL_LONG_MODE_DOTS = 1,
    LV_LABEL_LONG_SCROLL_CIRCULAR = 2
} lv_label_long_mode_t;

typedef enum {
    LV_ALIGN_DEFAULT = 0,
    LV_ALIGN_TOP_LEFT,
    LV_ALIGN_TOP_MID,
    LV_ALIGN_TOP_RIGHT,
    LV_ALIGN_BOTTOM_LEFT,
    LV_ALIGN_BOTTOM_MID,
    LV_ALIGN_BOTTOM_RIGHT,
    LV_ALIGN_LEFT_MID,
    LV_ALIGN_RIGHT_MID,
    LV_ALIGN_CENTER
} lv_align_t;

#define LV_OBJ_FLAG_HIDDEN (1u << 0)
#define LV_OBJ_FLAG_SCROLLABLE (1u << 4)

typedef struct _lv_obj_t lv_obj_t;

lv_obj_t* lv_obj_create(lv_obj_t* parent);
lv_obj_t* lv_label_create(lv_obj_t* parent);
lv_obj_t* lv_img_create(lv_obj_t* parent);
lv_obj_t* lv_image_create(lv_obj_t* parent);
void lv_obj_del(lv_obj_t* obj);
void lv_obj_clean(lv_obj_t* obj);
bool lv_obj_is_valid(const lv_obj_t* obj);
void lv_obj_set_parent(lv_obj_t* obj, lv_obj_t* parent);
size_t lv_obj_get_child_cnt(const lv_obj_t* obj);
lv_obj_t* lv_obj_get_child(const lv_obj_t* obj, size_t idx);

void lv_obj_set_size(lv_obj_t* obj, lv_coord_t w, lv_coord_t h);
void lv_obj_set_width(lv_obj_t* obj, lv_coord_t w);
void lv_obj_set_height(lv_obj_t* obj, lv_coord_t h);
lv_coord_t lv_obj_get_width(const lv_obj_t* obj);
lv_coord_t lv_obj_get_height(const lv_obj_t* obj);
void lv_obj_align(lv_obj_t* obj, lv_align_t align, lv_coord_t dx, lv_coord_t dy);
void lv_obj_center(lv_obj_t* obj);
void lv_obj_set_pos(lv_obj_t* obj, lv_coord_t x, lv_coord_t y);
void lv_obj_set_x(lv_obj_t* obj, lv_coord_t x);
void lv_obj_set_style_align(lv_obj_t* obj, lv_align_t align, uint32_t part);

void lv_obj_add_flag(lv_obj_t* obj, uint32_t flag);
void lv_obj_remove_flag(lv_obj_t* obj, uint32_t flag);
bool lv_obj_has_flag(const lv_obj_t* obj, uint32_t flag);

void lv_obj_set_style_text_font(lv_obj_t* obj, const lv_font_t* font, uint32_t part);
void lv_obj_set_style_text_color(lv_obj_t* obj, lv_color_t color, uint32_t part);
void lv_obj_set_style_bg_color(lv_obj_t* obj, lv_color_t color, uint32_t part);
void lv_obj_set_style_bg_opa(lv_obj_t* obj, lv_opa_t opa, uint32_t part);
void lv_obj_set_style_bg_image_src(lv_obj_t* obj, const void* src, uint32_t part);
void lv_obj_set_style_border_width(lv_obj_t* obj, lv_coord_t w, uint32_t part);
void lv_obj_set_style_border_color(lv_obj_t* obj, lv_color_t color, uint32_t part);
void lv_obj_set_style_radius(lv_obj_t* obj, lv_coord_t r, uint32_t part);
void lv_obj_set_style_pad_all(lv_obj_t* obj, lv_coord_t pad, uint32_t part);
void lv_obj_set_style_pad_top(lv_obj_t* obj, lv_coord_t pad, uint32_t part);
void lv_obj_set_style_pad_bottom(lv_obj_t* obj, lv_coord_t pad, uint32_t part);
void lv_obj_set_style_pad_left(lv_obj_t* obj, lv_coord_t pad, uint32_t part);
void lv_obj_set_style_pad_right(lv_obj_t* obj, lv_coord_t pad, uint32_t part);
void lv_obj_set_style_pad_row(lv_obj_t* obj, lv_coord_t pad, uint32_t part);
void lv_obj_set_style_pad_gap(lv_obj_t* obj, lv_coord_t pad, uint32_t part);
void lv_obj_set_style_margin_left(lv_obj_t* obj, lv_coord_t m, uint32_t part);
void lv_obj_set_style_scrollbar_mode(lv_obj_t* obj, int mode, uint32_t part);
void lv_obj_set_flex_flow(lv_obj_t* obj, lv_flex_flow_t flow);
void lv_obj_set_flex_grow(lv_obj_t* obj, uint8_t grow);
void lv_obj_set_flex_align(lv_obj_t* obj, lv_flex_align_t main, lv_flex_align_t cross,
                           lv_flex_align_t track);
void lv_obj_set_style_layout(lv_obj_t* obj, lv_layout_t layout, uint32_t part);
void lv_obj_set_style_text_align(lv_obj_t* obj, lv_text_align_t align, uint32_t part);
void lv_obj_set_scrollbar_mode(lv_obj_t* obj, int mode);
void lv_obj_set_user_data(lv_obj_t* obj, void* data);
void* lv_obj_get_user_data(const lv_obj_t* obj);

void lv_label_set_text(lv_obj_t* obj, const char* text);
const char* lv_label_get_text(const lv_obj_t* obj);
void lv_label_set_long_mode(lv_obj_t* obj, lv_label_long_mode_t mode);

lv_res_t lv_image_set_src(lv_obj_t* obj, const void* src);
void lv_image_set_scale(lv_obj_t* obj, uint16_t zoom);
uint16_t lv_image_get_scale(const lv_obj_t* obj);

void lv_obj_scroll_to_view_recursive(lv_obj_t* obj, int anim);
void lv_obj_update_layout(const lv_obj_t* obj);

/* animation surface (stored only) */
typedef struct {
    uint32_t delay;
    uint32_t repeat_count;
    uint32_t duration;
} lv_anim_t;
#define LV_ANIM_REPEAT_INFINITE 0xFFFFu
void lv_anim_init(lv_anim_t* a);
void lv_anim_set_delay(lv_anim_t* a, uint32_t delay);
void lv_anim_set_repeat_count(lv_anim_t* a, uint32_t count);
uint32_t lv_anim_speed_clamped(uint32_t speed, uint32_t min_time, uint32_t max_time);
void lv_obj_set_style_anim(lv_obj_t* obj, const lv_anim_t* a, uint32_t part);
void lv_obj_set_style_anim_duration(lv_obj_t* obj, uint32_t duration, uint32_t part);
void lv_anim_delete(lv_obj_t* obj, void* var);

/* events */
typedef enum { LV_EVENT_DELETE = 1 } lv_event_code_t;
typedef struct _lv_event_t {
    lv_event_code_t code;
    void* user_data;
    lv_obj_t* target;
} lv_event_t;
typedef void (*lv_event_cb_t)(lv_event_t* e);
void lv_obj_add_event_cb(lv_obj_t* obj, lv_event_cb_t cb, lv_event_code_t code, void* user_data);
void* lv_event_get_user_data(lv_event_t* e);

/* timers */
typedef struct _lv_timer_t lv_timer_t;
typedef void (*lv_timer_cb_t)(lv_timer_t* timer);
lv_timer_t* lv_timer_create(lv_timer_cb_t timer_xcb, uint32_t period, void* user_data);
void lv_timer_pause(lv_timer_t* timer);
void lv_timer_resume(lv_timer_t* timer);
void lv_timer_reset(lv_timer_t* timer);
void lv_timer_delete(lv_timer_t* timer);
void* lv_timer_get_user_data(const lv_timer_t* timer);

uint32_t lv_tick_get(void);
uint32_t lv_tick_elaps(uint32_t prev_tick);

/* ---------------------------------------------------------------- lv_fs */
/* Mirrors the vendored lv_fs.h enums/values; file IO is backed by host
 * FILE* (the GIF host path uses the in-memory data mode). */
typedef enum {
    LV_FS_MODE_WR = 0x01,
    LV_FS_MODE_RD = 0x02,
} lv_fs_mode_t;

typedef enum {
    LV_FS_RES_OK = 0,
    LV_FS_RES_TIMEOUT,
    LV_FS_RES_NOT_EX,
    LV_FS_RES_FULL,
    LV_FS_RES_LOCKED,
    LV_FS_RES_DENIED,
    LV_FS_RES_BUSY,
    LV_FS_RES_TOUT,
    LV_FS_RES_NOT_IMP,
    LV_FS_RES_OUT_OF_MEM,
    LV_FS_RES_INV_PARAM,
    LV_FS_RES_DRIVE_LETTER_ALREADY_USED,
    LV_FS_RES_UNKNOWN,
} lv_fs_res_t;

typedef enum {
    LV_FS_SEEK_SET = 0x00,
    LV_FS_SEEK_CUR = 0x01,
    LV_FS_SEEK_END = 0x02,
} lv_fs_whence_t;

typedef struct {
    void* file_d;
    void* drv;
    void* cache;
} lv_fs_file_t;

static inline lv_fs_res_t lv_fs_open(lv_fs_file_t* file_p, const char* path, lv_fs_mode_t mode) {
    (void)mode;
    FILE* stream = fopen(path, "rb");
    if (stream == NULL)
        return LV_FS_RES_NOT_EX;
    file_p->file_d = stream;
    return LV_FS_RES_OK;
}
static inline lv_fs_res_t lv_fs_close(lv_fs_file_t* file_p) {
    if (file_p->file_d == NULL)
        return LV_FS_RES_INV_PARAM;
    fclose((FILE*)file_p->file_d);
    file_p->file_d = NULL;
    return LV_FS_RES_OK;
}
static inline lv_fs_res_t lv_fs_read(lv_fs_file_t* file_p, void* buf, uint32_t btr, uint32_t* br) {
    if (file_p->file_d == NULL)
        return LV_FS_RES_INV_PARAM;
    size_t read = fread(buf, 1, btr, (FILE*)file_p->file_d);
    *br = (uint32_t)read;
    return LV_FS_RES_OK;
}
static inline lv_fs_res_t lv_fs_seek(lv_fs_file_t* file_p, uint32_t pos, lv_fs_whence_t whence) {
    if (file_p->file_d == NULL)
        return LV_FS_RES_INV_PARAM;
    int origin =
        whence == LV_FS_SEEK_CUR ? SEEK_CUR : (whence == LV_FS_SEEK_END ? SEEK_END : SEEK_SET);
    return fseek((FILE*)file_p->file_d, (long)pos, origin) == 0 ? LV_FS_RES_OK : LV_FS_RES_UNKNOWN;
}
static inline lv_fs_res_t lv_fs_tell(lv_fs_file_t* file_p, uint32_t* pos) {
    if (file_p->file_d == NULL)
        return LV_FS_RES_INV_PARAM;
    long current = ftell((FILE*)file_p->file_d);
    if (current < 0)
        return LV_FS_RES_UNKNOWN;
    *pos = (uint32_t)current;
    return LV_FS_RES_OK;
}

/* image decoder sniffing (used by LvglAllocatedImage) */
lv_res_t lv_image_decoder_get_info(const void* src, lv_image_header_t* header_out);

/* memory API (instrumented) */
void* lv_malloc(size_t size);
void* lv_realloc(void* ptr, size_t size);
void lv_free(void* ptr);

/* ------------------------------------------------------------- host hooks */
/* Display-lock state maintained by the esp_lvgl_port adapter. */
extern int g_lvgl_host_locked;

typedef enum {
    HOST_LVGL_EV_CREATE = 1,
    HOST_LVGL_EV_DELETE,
    HOST_LVGL_EV_ADD_FLAG,
    HOST_LVGL_EV_REM_FLAG,
    HOST_LVGL_EV_SET_TEXT,
    HOST_LVGL_EV_SET_SRC,
    HOST_LVGL_EV_SET_SCALE,
    HOST_LVGL_EV_STYLE,
    HOST_LVGL_EV_ALIGN,
    HOST_LVGL_EV_TIMER_CREATE,
    HOST_LVGL_EV_TIMER_PAUSE,
    HOST_LVGL_EV_TIMER_RESUME,
    HOST_LVGL_EV_TIMER_DELETE,
    HOST_LVGL_EV_LOCK_REJECTED,
} host_lvgl_event_kind_t;

void host_lvgl_reset_log(void);
size_t host_lvgl_log_count(int kind);
size_t host_lvgl_log_count_for(int kind, const lv_obj_t* obj);
int64_t host_lvgl_log_first_seq(int kind, const lv_obj_t* obj);
int64_t host_lvgl_log_last_seq(void);
size_t host_lvgl_unlocked_mutation_count(void);
size_t host_lvgl_nested_lock_rejections(void);

void host_lvgl_set_screen_size(uint32_t w, uint32_t h);
bool host_lvgl_obj_hidden(const lv_obj_t* obj);
const char* host_lvgl_obj_text(const lv_obj_t* obj);
const void* host_lvgl_obj_img_src(const lv_obj_t* obj);
uint16_t host_lvgl_obj_scale(const lv_obj_t* obj);
size_t host_lvgl_obj_scale_set_count(const lv_obj_t* obj);
uint32_t host_lvgl_obj_bg_opa(const lv_obj_t* obj);
uint32_t host_lvgl_obj_bg_color(const lv_obj_t* obj);
lv_coord_t host_lvgl_obj_x(const lv_obj_t* obj);
lv_coord_t host_lvgl_obj_y(const lv_obj_t* obj);
lv_coord_t host_lvgl_obj_w(const lv_obj_t* obj);
lv_coord_t host_lvgl_obj_h(const lv_obj_t* obj);
const lv_font_t* host_lvgl_obj_font(const lv_obj_t* obj);
size_t host_lvgl_live_obj_count(void);
bool host_lvgl_tree_has_text(const lv_obj_t* root, const char* needle);

/* lv_malloc instrumentation */
#define HOST_LVGL_BIG_ALLOC_THRESHOLD 200000u
void host_lvgl_alloc_reset(void);
size_t host_lvgl_big_alloc_count(void);
void host_lvgl_fail_next_allocations(size_t count);

#ifdef __cplusplus
}
#endif

#endif /* HOST_LVGL_SHIM_H */
