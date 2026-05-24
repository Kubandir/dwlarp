/* Wallpaper. Background-layer surface filling the bound output; decodes
 * WALL_PATH at configure time, bilinear cover-fit (max scale, center-crop),
 * blits once. Falls back to WALL_BG solid fill if the file is missing.
 *
 * Source pixels are freed after the blit — peak RSS during startup, steady-
 * state cost is just the SHM (out_w*out_h*4) same as swaybg. */

#include "twl.h"

#include <fcntl.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* stb_image config: PNG-only, in-memory only, no string error tables. */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_FAILURE_STRINGS
#include <stb/stb_image.h>

static char *expand_home(const char *p) {
    if (p[0] != '~' || p[1] != '/') return strdup(p);
    const char *home = getenv("HOME"); if (!home) home = "";
    size_t hl = strlen(home), pl = strlen(p);
    char *r = malloc(hl + pl);
    memcpy(r, home, hl); memcpy(r + hl, p + 1, pl - 1 + 1);
    return r;
}

static uint8_t *slurp(const char *path, int *len) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) < 0 || st.st_size <= 0 || st.st_size > (1 << 26)) {
        close(fd); return NULL;  /* >64MB PNG is almost certainly a mistake */
    }
    uint8_t *buf = malloc(st.st_size);
    if (!buf) { close(fd); return NULL; }
    int got = 0;
    while (got < st.st_size) {
        ssize_t r = read(fd, buf + got, st.st_size - got);
        if (r <= 0) { free(buf); close(fd); return NULL; }
        got += r;
    }
    close(fd);
    *len = (int)st.st_size;
    return buf;
}

/* Bilinear cover-fit. src is RGBA (stbi); dst is ARGB8888 little-endian
 * (== 0xAARRGGBB). Fixed-point Q16 sample step for the inner loop;
 * float-only on the per-row setup. */
static void blit_cover(uint32_t *dst, int dw, int dh,
                       const uint8_t *src, int sw, int sh) {
    double fx = (double)dw / sw, fy = (double)dh / sh;
    double scale = fx > fy ? fx : fy;
    double crop_x = (sw * scale - dw) / 2.0;
    double crop_y = (sh * scale - dh) / 2.0;

    /* Q16 source step per output pixel = 1/scale * 65536. */
    uint32_t step = (uint32_t)((65536.0 / scale) + 0.5);
    uint32_t base_x = (uint32_t)((crop_x / scale) * 65536.0 + 0.5);

    for (int oy = 0; oy < dh; oy++) {
        uint32_t syq = (uint32_t)(((oy + crop_y) / scale) * 65536.0 + 0.5);
        int iy = syq >> 16;
        uint32_t ty = syq & 0xffff;
        if (iy >= sh - 1) { iy = sh - 2; ty = 0xffff; }
        const uint8_t *r0 = src + (size_t)iy * sw * 4;
        const uint8_t *r1 = r0 + (size_t)sw * 4;
        uint32_t sxq = base_x;
        for (int ox = 0; ox < dw; ox++, sxq += step) {
            int ix = sxq >> 16;
            uint32_t tx = sxq & 0xffff;
            if (ix >= sw - 1) { ix = sw - 2; tx = 0xffff; }
            const uint8_t *p00 = r0 + ix * 4;
            const uint8_t *p10 = p00 + 4;
            const uint8_t *p01 = r1 + ix * 4;
            const uint8_t *p11 = p01 + 4;
            /* Bilinear weights in Q16. uint64 because (0x10000 * 0x10000)
             * overflows uint32 — happens whenever tx or ty is 0, which is
             * every pixel at scale=1.0. */
            uint64_t a = 0x10000u - tx, b_ = 0x10000u - ty;
            uint32_t w00 = (uint32_t)((a       * b_) >> 16);
            uint32_t w10 = (uint32_t)(((uint64_t)tx * b_) >> 16);
            uint32_t w01 = (uint32_t)((a       * (uint64_t)ty) >> 16);
            uint32_t w11 = (uint32_t)(((uint64_t)tx * ty) >> 16);
            uint32_t r = (p00[0]*w00 + p10[0]*w10 + p01[0]*w01 + p11[0]*w11) >> 16;
            uint32_t g = (p00[1]*w00 + p10[1]*w10 + p01[1]*w01 + p11[1]*w11) >> 16;
            uint32_t b = (p00[2]*w00 + p10[2]*w10 + p01[2]*w01 + p11[2]*w11) >> 16;
            dst[oy * dw + ox] = 0xff000000u | (r << 16) | (g << 8) | b;
        }
    }
}

void wall_render(Widget *w) {
    if (!w->configured || w->w <= 0 || w->h <= 0) return;
    widget_ensure_pool(w, 1);
    BufSlot *s = widget_free_slot(w);
    if (!s) return;

    char *path = expand_home(WALL_PATH);
    int flen = 0;
    uint8_t *fbuf = slurp(path, &flen);
    free(path);

    int sw = 0, sh = 0, sc = 0;
    uint8_t *src = NULL;
    if (fbuf) {
        src = stbi_load_from_memory(fbuf, flen, &sw, &sh, &sc, 4);
        free(fbuf);
    }

    if (src && sw > 1 && sh > 1) {
        blit_cover(s->px, w->w, w->h, src, sw, sh);
        stbi_image_free(src);
    } else {
        clear_buf(s->px, w->w, w->h, WALL_BG);
        if (fbuf) msg("twl: wallpaper decode failed (%s)", WALL_PATH);
    }
    /* Wallpaper attaches exactly once. The compositor will not fire
     * wl_buffer.release until a different buffer is attached — and we never
     * attach again — so we instead piggyback on wl_surface.frame: once
     * frame.done arrives we know the compositor has displayed (and uploaded)
     * this buffer at least once, so destroying the buffer + pool and
     * munmapping our SHM view is safe. The compositor keeps a GPU texture
     * reference, so the wallpaper stays on screen. on_frame_done (hud.c)
     * does the actual free. Saves ~8 MB on a 1080p output. */
    widget_attach(w, s, 1);
    w->want_pool_free = 1;

    /* Force glibc to return freed PNG-decode pages to the kernel — stb_image's
     * intermediate buffers can be tens of MB for high-res sources. */
    malloc_trim(0);
}

void wall_create_on(Output *o) {
    if (!o || o->wall) return;
    Widget *w = widget_alloc(W_WALL);
    if (!w) { msg("twl: no widget slot for wall"); return; }
    o->wall = w;
    widget_setup_surface(w, LAYER_BACKGROUND, "twl-wall", o);
    widget_set_anchor(w, LS_ANCHOR_TOP | LS_ANCHOR_BOTTOM
                      | LS_ANCHOR_LEFT | LS_ANCHOR_RIGHT);
    widget_set_size(w, 0, 0);
    widget_set_exclusive_zone(w, -1);  /* fill output regardless of bar */
    widget_set_kbd_interactive(w, 0);
    widget_set_input_region_rect(w, 0, 0, 0, 0);  /* click-through */
    wl_req(w->surface, SURFACE_REQ_COMMIT, NULL, 0, -1);
}
