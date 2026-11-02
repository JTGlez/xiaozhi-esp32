/* Host shim for the firmware GIF decoder. It provides only what
 * main/display/lvgl_display/gif/gifdec.c actually uses, so the real decoder can
 * be compiled and run on a workstation without LVGL or ESP-IDF.
 */
#ifndef HOST_LVGL_SHIM_H
#define HOST_LVGL_SHIM_H

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keep gifdec.c away from the Helium asm path (undefined macros would both
 * evaluate to 0 and match each other). */
#define LV_USE_DRAW_SW_ASM 0
#define LV_DRAW_SW_ASM_HELIUM 1

#define LV_FS_MODE_RD 0x01

typedef enum {
    LV_FS_SEEK_SET = 0,
    LV_FS_SEEK_CUR = 1,
    LV_FS_SEEK_END = 2,
} lv_fs_whence_t;

typedef enum {
    LV_FS_RES_OK = 0,
    LV_FS_RES_FAILED = 1,
    LV_FS_RES_NOT_EX = 2,
    LV_FS_RES_INV_PARAM = 5,
} lv_fs_res_t;

typedef FILE* lv_fs_file_t;

static inline lv_fs_res_t lv_fs_open(lv_fs_file_t* file_p, const char* path, uint32_t mode) {
    (void)mode;
    FILE* stream = fopen(path, "rb");
    if (stream == NULL) {
        return LV_FS_RES_NOT_EX;
    }
    *file_p = stream;
    return LV_FS_RES_OK;
}

static inline lv_fs_res_t lv_fs_read(lv_fs_file_t* file_p, void* buf, uint32_t btr, uint32_t* br) {
    const size_t read = fread(buf, 1, btr, *file_p);
    if (br != NULL) {
        *br = (uint32_t)read;
    }
    return read == btr ? LV_FS_RES_OK : LV_FS_RES_FAILED;
}

static inline lv_fs_res_t lv_fs_seek(lv_fs_file_t* file_p, uint32_t pos, lv_fs_whence_t whence) {
    int origin = SEEK_SET;
    if (whence == LV_FS_SEEK_CUR) {
        origin = SEEK_CUR;
    } else if (whence == LV_FS_SEEK_END) {
        origin = SEEK_END;
    }
    return fseek(*file_p, (long)pos, origin) == 0 ? LV_FS_RES_OK : LV_FS_RES_FAILED;
}

static inline lv_fs_res_t lv_fs_tell(lv_fs_file_t* file_p, uint32_t* pos) {
    const long current = ftell(*file_p);
    if (current < 0) {
        return LV_FS_RES_FAILED;
    }
    *pos = (uint32_t)current;
    return LV_FS_RES_OK;
}

static inline lv_fs_res_t lv_fs_close(lv_fs_file_t* file_p) {
    return fclose(*file_p) == 0 ? LV_FS_RES_OK : LV_FS_RES_FAILED;
}

static inline void* lv_malloc(size_t size) { return malloc(size); }

static inline void* lv_realloc(void* pointer, size_t size) { return realloc(pointer, size); }

static inline void lv_free(void* pointer) { free(pointer); }

#endif /* HOST_LVGL_SHIM_H */
