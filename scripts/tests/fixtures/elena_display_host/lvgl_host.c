/* Implementation of the host LVGL shim declared in lvgl.h. */
#include "esp_timer.h"
#include "lvgl.h"

#include <assert.h>
#include <stdio.h>

int g_lvgl_host_locked = 0;

/* ------------------------------------------------------------- event log */
#define HOST_LOG_CAPACITY 200000
typedef struct {
    int64_t seq;
    int kind;
    const void* obj;
    uintptr_t arg;
    int locked;
} host_log_entry_t;

static host_log_entry_t g_log[HOST_LOG_CAPACITY];
static size_t g_log_count = 0;
static int64_t g_log_seq = 0;

static size_t g_nested_lock_rejections = 0;

static void log_event(int kind, const void* obj, uintptr_t arg) {
    if (kind == HOST_LVGL_EV_LOCK_REJECTED && arg == 1) {
        ++g_nested_lock_rejections;
    }
    if (g_log_count < HOST_LOG_CAPACITY) {
        host_log_entry_t* e = &g_log[g_log_count++];
        e->seq = ++g_log_seq;
        e->kind = kind;
        e->obj = obj;
        e->arg = arg;
        e->locked = g_lvgl_host_locked;
    }
}

void host_lvgl_reset_log(void) { g_log_count = 0; }

size_t host_lvgl_log_count(int kind) {
    size_t n = 0;
    for (size_t i = 0; i < g_log_count; ++i) {
        if (g_log[i].kind == kind)
            ++n;
    }
    return n;
}

size_t host_lvgl_log_count_for(int kind, const lv_obj_t* obj) {
    size_t n = 0;
    for (size_t i = 0; i < g_log_count; ++i) {
        if (g_log[i].kind == kind && g_log[i].obj == obj)
            ++n;
    }
    return n;
}

int64_t host_lvgl_log_first_seq(int kind, const lv_obj_t* obj) {
    for (size_t i = 0; i < g_log_count; ++i) {
        if (g_log[i].kind == kind && g_log[i].obj == obj)
            return g_log[i].seq;
    }
    return -1;
}

int64_t host_lvgl_log_last_seq(void) { return g_log_seq; }

static int is_mutation_kind(int kind) {
    switch (kind) {
        case HOST_LVGL_EV_CREATE:
        case HOST_LVGL_EV_DELETE:
        case HOST_LVGL_EV_ADD_FLAG:
        case HOST_LVGL_EV_REM_FLAG:
        case HOST_LVGL_EV_SET_TEXT:
        case HOST_LVGL_EV_SET_SRC:
        case HOST_LVGL_EV_SET_SCALE:
        case HOST_LVGL_EV_STYLE:
        case HOST_LVGL_EV_TIMER_CREATE:
        case HOST_LVGL_EV_TIMER_PAUSE:
        case HOST_LVGL_EV_TIMER_RESUME:
        case HOST_LVGL_EV_TIMER_DELETE:
            return 1;
        default:
            return 0;
    }
}

size_t host_lvgl_unlocked_mutation_count(void) {
    size_t n = 0;
    for (size_t i = 0; i < g_log_count; ++i) {
        if (is_mutation_kind(g_log[i].kind) && !g_log[i].locked)
            ++n;
    }
    return n;
}

size_t host_lvgl_nested_lock_rejections(void) {
    /* Only rejections caused by taking the lock while already holding it
     * (arg==1); deliberate lock-failure injections (arg==0) are separate. */
    return g_nested_lock_rejections;
}

/* ---------------------------------------------------------------- objects */
#define LV_OBJ_MAGIC 0x4c564f42u
#define MAX_CHILDREN 64
#define MAX_EVENT_CBS 4

struct _lv_obj_t {
    uint32_t magic;
    lv_obj_t* parent;
    lv_obj_t* children[MAX_CHILDREN];
    size_t child_cnt;
    uint32_t flags;
    /* styles */
    lv_color_t bg_color;
    lv_opa_t bg_opa;
    lv_color_t text_color;
    const lv_font_t* font;
    lv_coord_t w;
    lv_coord_t h;
    lv_coord_t x;
    lv_coord_t y;
    lv_align_t align;
    lv_coord_t align_dx;
    lv_coord_t align_dy;
    /* label */
    char* text;
    lv_label_long_mode_t long_mode;
    /* image */
    const void* img_src;
    uint16_t scale;
    size_t scale_set_count;
    void* user_data;
    struct {
        lv_event_cb_t cb;
        lv_event_code_t code;
        void* user_data;
    } event_cbs[MAX_EVENT_CBS];
    size_t event_cb_cnt;
};

static lv_obj_t g_screen;
static lv_display_t g_display;
static bool g_display_valid = false;
static size_t g_created_obj_count = 0;

void host_lvgl_log_event(int kind, const void* obj, uintptr_t arg);
void host_lvgl_log_event(int kind, const void* obj, uintptr_t arg) { log_event(kind, obj, arg); }

static lv_obj_t* new_obj(lv_obj_t* parent, const char* initial_text) {
    lv_obj_t* o = (lv_obj_t*)calloc(1, sizeof(lv_obj_t));
    o->magic = LV_OBJ_MAGIC;
    o->parent = parent;
    o->bg_opa = LV_OPA_TRANSP;
    o->scale = 256;
    if (parent != NULL && parent->magic == LV_OBJ_MAGIC && parent->child_cnt < MAX_CHILDREN) {
        parent->children[parent->child_cnt++] = o;
    }
    if (initial_text != NULL)
        o->text = strdup(initial_text);
    ++g_created_obj_count;
    log_event(HOST_LVGL_EV_CREATE, o, (uintptr_t)parent);
    return o;
}

void lv_init(void) {
    memset(&g_screen, 0, sizeof(g_screen));
    g_screen.magic = LV_OBJ_MAGIC;
    g_screen.w = 320;
    g_screen.h = 240;
    g_screen.bg_opa = LV_OPA_COVER;
}

void host_lvgl_set_screen_size(uint32_t w, uint32_t h) {
    g_screen.w = (lv_coord_t)w;
    g_screen.h = (lv_coord_t)h;
}

lv_obj_t* lv_screen_active(void) { return &g_screen; }

lv_display_t* host_lvgl_create_display(uint32_t w, uint32_t h) {
    g_display.hor_res = w;
    g_display.ver_res = h;
    g_display.alive = true;
    g_display_valid = true;
    host_lvgl_set_screen_size(w, h);
    return &g_display;
}

lv_display_t* lv_display_get_default(void) { return g_display_valid ? &g_display : NULL; }

uint32_t lv_display_get_horizontal_resolution(const lv_display_t* disp) {
    return disp != NULL ? disp->hor_res : 0;
}
uint32_t lv_display_get_vertical_resolution(const lv_display_t* disp) {
    return disp != NULL ? disp->ver_res : 0;
}
void lv_display_set_offset(lv_display_t* disp, lv_coord_t x, lv_coord_t y) {
    (void)disp;
    (void)x;
    (void)y;
}
void lv_display_delete(lv_display_t* disp) {
    if (disp != NULL)
        disp->alive = false;
    if (disp == &g_display)
        g_display_valid = false;
}
lv_res_t lv_image_cache_resize(uint32_t size, bool user_cache) {
    (void)size;
    (void)user_cache;
    return LV_RESULT_OK;
}

lv_obj_t* lv_obj_create(lv_obj_t* parent) { return new_obj(parent, NULL); }
lv_obj_t* lv_label_create(lv_obj_t* parent) { return new_obj(parent, ""); }
lv_obj_t* lv_img_create(lv_obj_t* parent) { return new_obj(parent, NULL); }
lv_obj_t* lv_image_create(lv_obj_t* parent) { return new_obj(parent, NULL); }

static void detach_from_parent(lv_obj_t* o) {
    lv_obj_t* p = o->parent;
    if (p == NULL || p->magic != LV_OBJ_MAGIC)
        return;
    for (size_t i = 0; i < p->child_cnt; ++i) {
        if (p->children[i] == o) {
            for (size_t k = i + 1; k < p->child_cnt; ++k)
                p->children[k - 1] = p->children[k];
            --p->child_cnt;
            break;
        }
    }
    o->parent = NULL;
}

void lv_obj_del(lv_obj_t* obj) {
    if (obj == NULL || obj->magic != LV_OBJ_MAGIC)
        return;
    log_event(HOST_LVGL_EV_DELETE, obj, 0);
    while (obj->child_cnt > 0)
        lv_obj_del(obj->children[0]);
    for (size_t i = 0; i < obj->event_cb_cnt; ++i) {
        if (obj->event_cbs[i].code == LV_EVENT_DELETE && obj->event_cbs[i].cb != NULL) {
            lv_event_t ev;
            ev.code = LV_EVENT_DELETE;
            ev.user_data = obj->event_cbs[i].user_data;
            ev.target = obj;
            obj->event_cbs[i].cb(&ev);
        }
    }
    detach_from_parent(obj);
    free(obj->text);
    obj->magic = 0;
    free(obj);
}

void lv_obj_clean(lv_obj_t* obj) {
    if (obj == NULL || obj->magic != LV_OBJ_MAGIC)
        return;
    while (obj->child_cnt > 0)
        lv_obj_del(obj->children[0]);
}

bool lv_obj_is_valid(const lv_obj_t* obj) { return obj != NULL && obj->magic == LV_OBJ_MAGIC; }

void lv_obj_set_parent(lv_obj_t* obj, lv_obj_t* parent) {
    if (!lv_obj_is_valid(obj))
        return;
    detach_from_parent(obj);
    obj->parent = parent;
    if (lv_obj_is_valid(parent) && parent->child_cnt < MAX_CHILDREN) {
        parent->children[parent->child_cnt++] = obj;
    }
}

size_t lv_obj_get_child_cnt(const lv_obj_t* obj) {
    return lv_obj_is_valid(obj) ? obj->child_cnt : 0;
}
lv_obj_t* lv_obj_get_child(const lv_obj_t* obj, size_t idx) {
    if (!lv_obj_is_valid(obj) || idx >= obj->child_cnt)
        return NULL;
    return obj->children[idx];
}

static void apply_align(lv_obj_t* o) {
    const lv_obj_t* ref = lv_obj_is_valid(o->parent) ? o->parent : &g_screen;
    lv_coord_t w = o->w;
    lv_coord_t h = o->h;
    if (w == LV_SIZE_CONTENT)
        w = 0;
    if (h == LV_SIZE_CONTENT)
        h = 0;
    switch (o->align) {
        case LV_ALIGN_TOP_MID:
            o->x = ref->w / 2 - w / 2 + o->align_dx;
            o->y = ref->y + o->align_dy;
            break;
        case LV_ALIGN_BOTTOM_MID:
            o->x = ref->w / 2 - w / 2 + o->align_dx;
            o->y = ref->y + ref->h - h + o->align_dy;
            break;
        case LV_ALIGN_TOP_LEFT:
        case LV_ALIGN_DEFAULT:
            o->x = ref->x + o->align_dx;
            o->y = ref->y + o->align_dy;
            break;
        case LV_ALIGN_LEFT_MID:
            o->x = ref->x + o->align_dx;
            o->y = ref->y + ref->h / 2 - h / 2 + o->align_dy;
            break;
        case LV_ALIGN_RIGHT_MID:
            o->x = ref->x + ref->w - w + o->align_dx;
            o->y = ref->y + ref->h / 2 - h / 2 + o->align_dy;
            break;
        case LV_ALIGN_CENTER:
        default:
            o->x = ref->w / 2 - w / 2 + o->align_dx;
            o->y = ref->h / 2 - h / 2 + o->align_dy;
            break;
    }
}

void lv_obj_set_size(lv_obj_t* obj, lv_coord_t w, lv_coord_t h) {
    if (!lv_obj_is_valid(obj))
        return;
    obj->w = w;
    obj->h = h;
    apply_align(obj);
}
void lv_obj_set_width(lv_obj_t* obj, lv_coord_t w) {
    if (!lv_obj_is_valid(obj))
        return;
    obj->w = w;
    apply_align(obj);
}
void lv_obj_set_height(lv_obj_t* obj, lv_coord_t h) {
    if (!lv_obj_is_valid(obj))
        return;
    obj->h = h;
    apply_align(obj);
}
lv_coord_t lv_obj_get_width(const lv_obj_t* obj) { return lv_obj_is_valid(obj) ? obj->w : 0; }
lv_coord_t lv_obj_get_height(const lv_obj_t* obj) { return lv_obj_is_valid(obj) ? obj->h : 0; }

void lv_obj_align(lv_obj_t* obj, lv_align_t align, lv_coord_t dx, lv_coord_t dy) {
    if (!lv_obj_is_valid(obj))
        return;
    obj->align = align;
    obj->align_dx = dx;
    obj->align_dy = dy;
    apply_align(obj);
    log_event(HOST_LVGL_EV_ALIGN, obj, (uintptr_t)align);
}
void lv_obj_center(lv_obj_t* obj) { lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0); }

void lv_obj_add_flag(lv_obj_t* obj, uint32_t flag) {
    if (!lv_obj_is_valid(obj))
        return;
    obj->flags |= flag;
    log_event(HOST_LVGL_EV_ADD_FLAG, obj, flag);
}
void lv_obj_remove_flag(lv_obj_t* obj, uint32_t flag) {
    if (!lv_obj_is_valid(obj))
        return;
    obj->flags &= ~flag;
    log_event(HOST_LVGL_EV_REM_FLAG, obj, flag);
}
bool lv_obj_has_flag(const lv_obj_t* obj, uint32_t flag) {
    return lv_obj_is_valid(obj) && (obj->flags & flag) != 0;
}

#define STYLE_EVENT(obj) log_event(HOST_LVGL_EV_STYLE, obj, 0)

void lv_obj_set_style_text_font(lv_obj_t* obj, const lv_font_t* font, uint32_t part) {
    (void)part;
    if (!lv_obj_is_valid(obj))
        return;
    obj->font = font;
    STYLE_EVENT(obj);
}
void lv_obj_set_style_text_color(lv_obj_t* obj, lv_color_t color, uint32_t part) {
    (void)part;
    if (!lv_obj_is_valid(obj))
        return;
    obj->text_color = color;
    STYLE_EVENT(obj);
}
void lv_obj_set_style_bg_color(lv_obj_t* obj, lv_color_t color, uint32_t part) {
    (void)part;
    if (!lv_obj_is_valid(obj))
        return;
    obj->bg_color = color;
    STYLE_EVENT(obj);
}
void lv_obj_set_style_bg_opa(lv_obj_t* obj, lv_opa_t opa, uint32_t part) {
    (void)part;
    if (!lv_obj_is_valid(obj))
        return;
    obj->bg_opa = opa;
    STYLE_EVENT(obj);
}
void lv_obj_set_style_bg_image_src(lv_obj_t* obj, const void* src, uint32_t part) {
    (void)part;
    (void)src;
    if (!lv_obj_is_valid(obj))
        return;
    STYLE_EVENT(obj);
}
void lv_obj_set_style_border_width(lv_obj_t* obj, lv_coord_t w, uint32_t part) {
    (void)part;
    (void)w;
    if (lv_obj_is_valid(obj))
        STYLE_EVENT(obj);
}
void lv_obj_set_style_border_color(lv_obj_t* obj, lv_color_t c, uint32_t part) {
    (void)part;
    (void)c;
    if (lv_obj_is_valid(obj))
        STYLE_EVENT(obj);
}
void lv_obj_set_style_radius(lv_obj_t* obj, lv_coord_t r, uint32_t part) {
    (void)part;
    (void)r;
    if (lv_obj_is_valid(obj))
        STYLE_EVENT(obj);
}
void lv_obj_set_style_pad_all(lv_obj_t* obj, lv_coord_t p, uint32_t part) {
    (void)part;
    (void)p;
    if (lv_obj_is_valid(obj))
        STYLE_EVENT(obj);
}
void lv_obj_set_style_pad_top(lv_obj_t* obj, lv_coord_t p, uint32_t part) {
    (void)part;
    (void)p;
    if (lv_obj_is_valid(obj))
        STYLE_EVENT(obj);
}
void lv_obj_set_style_pad_bottom(lv_obj_t* obj, lv_coord_t p, uint32_t part) {
    (void)part;
    (void)p;
    if (lv_obj_is_valid(obj))
        STYLE_EVENT(obj);
}
void lv_obj_set_style_pad_left(lv_obj_t* obj, lv_coord_t p, uint32_t part) {
    (void)part;
    (void)p;
    if (lv_obj_is_valid(obj))
        STYLE_EVENT(obj);
}
void lv_obj_set_style_pad_right(lv_obj_t* obj, lv_coord_t p, uint32_t part) {
    (void)part;
    (void)p;
    if (lv_obj_is_valid(obj))
        STYLE_EVENT(obj);
}
void lv_obj_set_style_pad_row(lv_obj_t* obj, lv_coord_t p, uint32_t part) {
    (void)part;
    (void)p;
    if (lv_obj_is_valid(obj))
        STYLE_EVENT(obj);
}
void lv_obj_set_style_pad_gap(lv_obj_t* obj, lv_coord_t p, uint32_t part) {
    (void)part;
    (void)p;
    if (lv_obj_is_valid(obj))
        STYLE_EVENT(obj);
}
void lv_obj_set_style_margin_left(lv_obj_t* obj, lv_coord_t m, uint32_t part) {
    (void)part;
    (void)m;
    if (lv_obj_is_valid(obj))
        STYLE_EVENT(obj);
}
void lv_obj_set_style_scrollbar_mode(lv_obj_t* obj, int mode, uint32_t part) {
    (void)part;
    (void)mode;
    if (lv_obj_is_valid(obj))
        STYLE_EVENT(obj);
}
void lv_obj_set_flex_grow(lv_obj_t* obj, uint8_t grow) {
    (void)obj;
    (void)grow;
}

void lv_obj_set_flex_flow(lv_obj_t* obj, lv_flex_flow_t flow) {
    (void)flow;
    if (lv_obj_is_valid(obj))
        STYLE_EVENT(obj);
}
void lv_obj_set_flex_align(lv_obj_t* obj, lv_flex_align_t a, lv_flex_align_t b, lv_flex_align_t c) {
    (void)a;
    (void)b;
    (void)c;
    if (lv_obj_is_valid(obj))
        STYLE_EVENT(obj);
}
void lv_obj_set_style_layout(lv_obj_t* obj, lv_layout_t layout, uint32_t part) {
    (void)part;
    (void)layout;
    if (lv_obj_is_valid(obj))
        STYLE_EVENT(obj);
}
void lv_obj_set_style_text_align(lv_obj_t* obj, lv_text_align_t align, uint32_t part) {
    (void)part;
    (void)align;
    if (lv_obj_is_valid(obj))
        STYLE_EVENT(obj);
}
void lv_obj_set_scrollbar_mode(lv_obj_t* obj, int mode) {
    (void)mode;
    if (lv_obj_is_valid(obj))
        STYLE_EVENT(obj);
}
void lv_obj_set_user_data(lv_obj_t* obj, void* data) {
    if (lv_obj_is_valid(obj))
        obj->user_data = data;
}
void* lv_obj_get_user_data(const lv_obj_t* obj) {
    return lv_obj_is_valid(obj) ? obj->user_data : NULL;
}

void lv_label_set_text(lv_obj_t* obj, const char* text) {
    if (!lv_obj_is_valid(obj))
        return;
    free(obj->text);
    obj->text = strdup(text != NULL ? text : "");
    log_event(HOST_LVGL_EV_SET_TEXT, obj, (uintptr_t)obj->text);
}
const char* lv_label_get_text(const lv_obj_t* obj) {
    return lv_obj_is_valid(obj) && obj->text != NULL ? obj->text : "";
}
void lv_label_set_long_mode(lv_obj_t* obj, lv_label_long_mode_t mode) {
    if (lv_obj_is_valid(obj)) {
        obj->long_mode = mode;
        STYLE_EVENT(obj);
    }
}

void lv_text_get_size(lv_point_t* size_res, const char* text, const lv_font_t* font,
                      int32_t letter_space, int32_t line_space, int32_t max_width,
                      lv_text_flag_t flag) {
    (void)font;
    (void)letter_space;
    (void)line_space;
    (void)max_width;
    (void)flag;
    size_res->x = (int32_t)strlen(text) * 10;
    size_res->y = 20;
}

lv_res_t lv_image_set_src(lv_obj_t* obj, const void* src) {
    if (!lv_obj_is_valid(obj))
        return LV_RESULT_INVALID;
    obj->img_src = src;
    log_event(HOST_LVGL_EV_SET_SRC, obj, (uintptr_t)src);
    return LV_RESULT_OK;
}
void lv_image_set_scale(lv_obj_t* obj, uint16_t zoom) {
    if (!lv_obj_is_valid(obj))
        return;
    obj->scale = zoom;
    ++obj->scale_set_count;
    log_event(HOST_LVGL_EV_SET_SCALE, obj, zoom);
}
uint16_t lv_image_get_scale(const lv_obj_t* obj) { return lv_obj_is_valid(obj) ? obj->scale : 256; }

void lv_obj_scroll_to_view_recursive(lv_obj_t* obj, int anim) {
    (void)obj;
    (void)anim;
}
void lv_obj_update_layout(const lv_obj_t* obj) { (void)obj; }

void lv_anim_init(lv_anim_t* a) { memset(a, 0, sizeof(*a)); }
void lv_anim_set_delay(lv_anim_t* a, uint32_t delay) { a->delay = delay; }
void lv_anim_set_repeat_count(lv_anim_t* a, uint32_t count) { a->repeat_count = count; }
uint32_t lv_anim_speed_clamped(uint32_t speed, uint32_t min_time, uint32_t max_time) {
    (void)speed;
    (void)min_time;
    (void)max_time;
    return 300;
}
void lv_obj_set_style_anim(lv_obj_t* obj, const lv_anim_t* a, uint32_t part) {
    (void)part;
    (void)a;
    if (lv_obj_is_valid(obj))
        STYLE_EVENT(obj);
}
void lv_obj_set_style_anim_duration(lv_obj_t* obj, uint32_t d, uint32_t part) {
    (void)part;
    (void)d;
    if (lv_obj_is_valid(obj))
        STYLE_EVENT(obj);
}
void lv_anim_delete(lv_obj_t* obj, void* var) {
    (void)obj;
    (void)var;
}

void lv_obj_add_event_cb(lv_obj_t* obj, lv_event_cb_t cb, lv_event_code_t code, void* user_data) {
    if (!lv_obj_is_valid(obj) || obj->event_cb_cnt >= MAX_EVENT_CBS)
        return;
    obj->event_cbs[obj->event_cb_cnt].cb = cb;
    obj->event_cbs[obj->event_cb_cnt].code = code;
    obj->event_cbs[obj->event_cb_cnt].user_data = user_data;
    ++obj->event_cb_cnt;
}
void* lv_event_get_user_data(lv_event_t* e) { return e->user_data; }

/* ---------------------------------------------------------------- timers */
#define MAX_LV_TIMERS 64
struct _lv_timer_t {
    bool alive;
    bool paused;
    lv_timer_cb_t cb;
    uint32_t period;
    void* user_data;
};
static lv_timer_t g_lv_timers[MAX_LV_TIMERS];

lv_timer_t* lv_timer_create(lv_timer_cb_t timer_xcb, uint32_t period, void* user_data) {
    for (size_t i = 0; i < MAX_LV_TIMERS; ++i) {
        if (!g_lv_timers[i].alive) {
            lv_timer_t* t = &g_lv_timers[i];
            t->alive = true;
            t->paused = false;
            t->cb = timer_xcb;
            t->period = period;
            t->user_data = user_data;
            log_event(HOST_LVGL_EV_TIMER_CREATE, t, (uintptr_t)user_data);
            return t;
        }
    }
    return NULL;
}
void lv_timer_pause(lv_timer_t* timer) {
    if (timer != NULL && timer->alive) {
        timer->paused = true;
        log_event(HOST_LVGL_EV_TIMER_PAUSE, timer, 0);
    }
}
void lv_timer_resume(lv_timer_t* timer) {
    if (timer != NULL && timer->alive) {
        timer->paused = false;
        log_event(HOST_LVGL_EV_TIMER_RESUME, timer, 0);
    }
}
void lv_timer_reset(lv_timer_t* timer) { (void)timer; }
void lv_timer_delete(lv_timer_t* timer) {
    if (timer != NULL && timer->alive) {
        log_event(HOST_LVGL_EV_TIMER_DELETE, timer, 0);
        timer->alive = false;
    }
}
void* lv_timer_get_user_data(const lv_timer_t* timer) {
    return timer != NULL ? timer->user_data : NULL;
}

uint32_t lv_tick_get(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
uint32_t lv_tick_elaps(uint32_t prev_tick) { return lv_tick_get() - prev_tick; }

/* ------------------------------------------------------------ font stubs */
bool lv_font_get_glyph_dsc_fmt_txt(const lv_font_t* font, uint32_t letter, uint32_t letter_next,
                                   void* glyph_out) {
    (void)font;
    (void)letter;
    (void)letter_next;
    (void)glyph_out;
    return false;
}
const void* lv_font_get_bitmap_fmt_txt(const void* dsc, uint32_t glyph_index) {
    (void)dsc;
    (void)glyph_index;
    return NULL;
}

/* ------------------------------------------------------ decoder sniffing */
lv_res_t lv_image_decoder_get_info(const void* src, lv_image_header_t* header_out) {
    const uint8_t* bytes = (const uint8_t*)src;
    if (src == NULL || memcmp(bytes, "GIF8", 4) != 0)
        return LV_RESULT_INVALID;
    header_out->magic = LV_IMAGE_HEADER_MAGIC;
    header_out->cf = LV_COLOR_FORMAT_RAW_ALPHA;
    header_out->w = (uint16_t)(bytes[6] | (bytes[7] << 8));
    header_out->h = (uint16_t)(bytes[8] | (bytes[9] << 8));
    header_out->stride = 0;
    return LV_RESULT_OK;
}

/* ---------------------------------------------------------------- memory */
static size_t g_fail_alloc_remaining = 0;
static size_t g_big_alloc_attempts = 0;

void host_lvgl_alloc_reset(void) {
    g_fail_alloc_remaining = 0;
    g_big_alloc_attempts = 0;
}
size_t host_lvgl_big_alloc_count(void) { return g_big_alloc_attempts; }
void host_lvgl_fail_next_allocations(size_t count) { g_fail_alloc_remaining = count; }

void* lv_malloc(size_t size) {
    if (size >= HOST_LVGL_BIG_ALLOC_THRESHOLD)
        ++g_big_alloc_attempts;
    if (g_fail_alloc_remaining > 0) {
        --g_fail_alloc_remaining;
        return NULL;
    }
    return malloc(size);
}
void* lv_realloc(void* ptr, size_t size) { return realloc(ptr, size); }
void lv_free(void* ptr) { free(ptr); }

/* ------------------------------------------------------------- accessors */
bool host_lvgl_obj_hidden(const lv_obj_t* obj) { return lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN); }
const char* host_lvgl_obj_text(const lv_obj_t* obj) { return lv_label_get_text(obj); }
const void* host_lvgl_obj_img_src(const lv_obj_t* obj) {
    return lv_obj_is_valid(obj) ? obj->img_src : NULL;
}
uint16_t host_lvgl_obj_scale(const lv_obj_t* obj) { return lv_image_get_scale(obj); }
size_t host_lvgl_obj_scale_set_count(const lv_obj_t* obj) {
    return lv_obj_is_valid(obj) ? obj->scale_set_count : 0;
}
uint32_t host_lvgl_obj_bg_opa(const lv_obj_t* obj) {
    return lv_obj_is_valid(obj) ? (uint32_t)obj->bg_opa : 0;
}
uint32_t host_lvgl_obj_bg_color(const lv_obj_t* obj) {
    return lv_obj_is_valid(obj) ? obj->bg_color.val : 0;
}
lv_coord_t host_lvgl_obj_x(const lv_obj_t* obj) { return lv_obj_is_valid(obj) ? obj->x : 0; }
lv_coord_t host_lvgl_obj_y(const lv_obj_t* obj) { return lv_obj_is_valid(obj) ? obj->y : 0; }
lv_coord_t host_lvgl_obj_w(const lv_obj_t* obj) { return lv_obj_get_width(obj); }
lv_coord_t host_lvgl_obj_h(const lv_obj_t* obj) { return lv_obj_get_height(obj); }
uint32_t host_lvgl_obj_text_color(const lv_obj_t* obj) {
    return lv_obj_is_valid(obj) ? obj->text_color.val : 0;
}
lv_label_long_mode_t host_lvgl_obj_long_mode(const lv_obj_t* obj) {
    return lv_obj_is_valid(obj) ? obj->long_mode : LV_LABEL_LONG_WRAP;
}
const lv_obj_t* host_lvgl_obj_parent(const lv_obj_t* obj) {
    return lv_obj_is_valid(obj) ? obj->parent : NULL;
}
const lv_font_t* host_lvgl_obj_font(const lv_obj_t* obj) {
    return lv_obj_is_valid(obj) ? obj->font : NULL;
}
size_t host_lvgl_live_obj_count(void) { return g_created_obj_count; }

bool host_lvgl_tree_has_text(const lv_obj_t* root, const char* needle) {
    if (!lv_obj_is_valid(root) || lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN))
        return false;
    if (root->text != NULL && strstr(root->text, needle) != NULL)
        return true;
    for (size_t i = 0; i < root->child_cnt; ++i) {
        if (host_lvgl_tree_has_text(root->children[i], needle))
            return true;
    }
    return false;
}

void lv_obj_set_pos(lv_obj_t* obj, lv_coord_t x, lv_coord_t y) {
    if (!lv_obj_is_valid(obj))
        return;
    obj->x = x;
    obj->y = y;
    log_event(HOST_LVGL_EV_ALIGN, obj, 0);
}
void lv_obj_set_x(lv_obj_t* obj, lv_coord_t x) {
    if (!lv_obj_is_valid(obj))
        return;
    obj->x = x;
    log_event(HOST_LVGL_EV_ALIGN, obj, 0);
}
void lv_obj_set_style_align(lv_obj_t* obj, lv_align_t align, uint32_t part) {
    (void)part;
    lv_obj_align(obj, align, 0, 0);
}
