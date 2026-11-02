/* Host driver for the firmware GIF decoder (main/display/lvgl_display/gif).
 *
 * inspect <gif>            decoder dimensions, per-frame hashes, loop boundaries
 * rgb565  <gif> <out>      first frame as raw RGB565 little endian
 *
 * Loops are detected the same way the firmware does: the decoder's read
 * position jumps back when the animation wraps.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gifdec.h"

#define FNV_OFFSET 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL
#define MIX_PRIME 0x9e3779b97f4a7c15ULL

#define MAX_FRAMES 400
#define MAX_LOOPS 2

static uint64_t hash_a(const uint8_t* pixels, size_t length, uint64_t seed) {
    uint64_t hash = seed ^ FNV_OFFSET;
    for (size_t i = 0; i < length; ++i) {
        hash = (hash ^ pixels[i]) * FNV_PRIME;
    }
    return hash;
}

static uint64_t hash_b(const uint8_t* pixels, size_t length, uint64_t seed) {
    uint64_t hash = seed ^ 0x2545f4914f6cdd1dULL;
    for (size_t i = 0; i < length; ++i) {
        hash += pixels[i];
        hash *= MIX_PRIME;
        hash ^= hash >> 31;
    }
    return hash;
}

static uint8_t* read_whole_file(const char* path, size_t* out_size) {
    FILE* stream = fopen(path, "rb");
    if (stream == NULL) {
        fprintf(stderr, "cannot read %s\n", path);
        return NULL;
    }
    if (fseek(stream, 0, SEEK_END) != 0) {
        fclose(stream);
        return NULL;
    }
    const long size = ftell(stream);
    if (size <= 0) {
        fclose(stream);
        return NULL;
    }
    rewind(stream);
    uint8_t* bytes = (uint8_t*)malloc((size_t)size);
    if (bytes == NULL || fread(bytes, 1, (size_t)size, stream) != (size_t)size) {
        free(bytes);
        fclose(stream);
        return NULL;
    }
    fclose(stream);
    *out_size = (size_t)size;
    return bytes;
}

static int usage(const char* program) {
    fprintf(stderr, "usage: %s inspect <gif>\n", program);
    fprintf(stderr, "       %s rgb565 <gif> <out>\n", program);
    return 2;
}

static int inspect(gd_GIF* gif) {
    printf("dim %u %u\n", gif->width, gif->height);

    const size_t plane = (size_t)gif->width * gif->height * 4;
    unsigned long loops = 0;
    unsigned long index_in_loop = 0;
    unsigned long total = 0;
    unsigned long loop_frames[MAX_LOOPS + 1] = {0};

    while (total < MAX_FRAMES) {
        const uint32_t position_before = gif->f_rw_p;
        const int has_frame = gd_get_frame(gif);
        if (has_frame == 0) {
            break;
        }
        if (has_frame < 0) {
            fprintf(stderr, "decoder error in a gif frame\n");
            return 1;
        }
        if (gif->f_rw_p < position_before) {
            loop_frames[loops] = index_in_loop;
            loops++;
            index_in_loop = 0;
            if (loops >= MAX_LOOPS) {
                break;
            }
        }
        gd_render_frame(gif, gif->canvas);
        const uint64_t seed = (uint64_t)index_in_loop * FNV_PRIME;
        printf("frame %lu %lu %016llx %016llx\n", loops, index_in_loop,
               (unsigned long long)hash_a(gif->canvas, plane, seed),
               (unsigned long long)hash_b(gif->canvas, plane, seed));
        index_in_loop++;
        total++;
    }

    if (loops <= MAX_LOOPS) {
        loop_frames[loops] = index_in_loop;
        loops++;
    }
    for (unsigned long loop = 0; loop < loops; ++loop) {
        printf("loop %lu frames %lu\n", loop, loop_frames[loop]);
    }
    printf("frames %lu\n", total);
    return 0;
}

static int first_frame_rgb565(gd_GIF* gif, const char* output) {
    if (gd_get_frame(gif) != 1) {
        fprintf(stderr, "the gif has no decodable first frame\n");
        return 1;
    }
    gd_render_frame(gif, gif->canvas);

    const size_t pixels = (size_t)gif->width * gif->height;
    uint8_t* rgb565 = (uint8_t*)malloc(pixels * 2);
    if (rgb565 == NULL) {
        fprintf(stderr, "cannot allocate rgb565 buffer\n");
        return 1;
    }
    for (size_t i = 0; i < pixels; ++i) {
        const uint8_t blue = gif->canvas[i * 4 + 0];
        const uint8_t green = gif->canvas[i * 4 + 1];
        const uint8_t red = gif->canvas[i * 4 + 2];
        const uint16_t value = (uint16_t)(((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3));
        rgb565[i * 2 + 0] = (uint8_t)(value & 0xff);
        rgb565[i * 2 + 1] = (uint8_t)(value >> 8);
    }

    FILE* stream = fopen(output, "wb");
    if (stream == NULL) {
        free(rgb565);
        fprintf(stderr, "cannot write %s\n", output);
        return 1;
    }
    const size_t written = fwrite(rgb565, 1, pixels * 2, stream);
    fclose(stream);
    free(rgb565);
    if (written != pixels * 2) {
        fprintf(stderr, "short write on %s\n", output);
        return 1;
    }
    printf("rgb565 %u %u %lu\n", gif->width, gif->height, (unsigned long)(pixels * 2));
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        return usage(argv[0]);
    }

    size_t size = 0;
    uint8_t* bytes = read_whole_file(argv[2], &size);
    if (bytes == NULL) {
        return 1;
    }

    gd_GIF* gif = gd_open_gif_data(bytes);
    if (gif == NULL) {
        fprintf(stderr, "cannot decode %s\n", argv[2]);
        free(bytes);
        return 1;
    }

    int result = 2;
    if (strcmp(argv[1], "inspect") == 0) {
        result = inspect(gif);
    } else if (strcmp(argv[1], "rgb565") == 0 && argc >= 4) {
        result = first_frame_rgb565(gif, argv[3]);
    } else if (strcmp(argv[1], "frames") == 0 && argc == 5) {
        FILE* output = fopen(argv[3], "wb");
        result = output == NULL;
        for (int i = 0; !result && i < atoi(argv[4]); ++i) {
            if (gd_get_frame(gif) != 1) {
                result = 1;
                break;
            }
            gd_render_frame(gif, gif->canvas);
            size_t length = (size_t)gif->width * gif->height * 4;
            if (fwrite(gif->canvas, 1, length, output) != length)
                result = 1;
        }
        if (output)
            fclose(output);
    } else {
        result = usage(argv[0]);
    }

    gd_close_gif(gif);
    free(bytes);
    return result;
}
