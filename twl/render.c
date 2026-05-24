/* Drawing primitives: alpha8 glyph blit, premultiplied rect fills, UTF-8. */
#include "twl.h"

#include <string.h>

void clear_buf(uint32_t *px, int w, int h, uint32_t c) {
    int n = w * h;
    for (int i = 0; i < n; i++) px[i] = c;
}

void fill_rect(uint32_t *px, int sw, int sh, int x, int y, int w, int h, uint32_t c) {
    int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y;
    int x1 = x + w > sw ? sw : x + w;
    int y1 = y + h > sh ? sh : y + h;
    if (x0 >= x1 || y0 >= y1) return;
    for (int j = y0; j < y1; j++) {
        uint32_t *row = px + j * sw;
        for (int i = x0; i < x1; i++) row[i] = c;
    }
}

/* Decode one UTF-8 codepoint. Returns bytes consumed (0 if invalid). */
int utf8_decode(const char *s, uint32_t *cp) {
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) { *cp = c; return c ? 1 : 0; }
    if ((c & 0xe0) == 0xc0) {
        if ((s[1] & 0xc0) != 0x80) return 0;
        *cp = ((c & 0x1f) << 6) | (s[1] & 0x3f);
        return 2;
    }
    if ((c & 0xf0) == 0xe0) {
        if ((s[1] & 0xc0) != 0x80 || (s[2] & 0xc0) != 0x80) return 0;
        *cp = ((c & 0x0f) << 12) | ((s[1] & 0x3f) << 6) | (s[2] & 0x3f);
        return 3;
    }
    if ((c & 0xf8) == 0xf0) {
        if ((s[1] & 0xc0) != 0x80 || (s[2] & 0xc0) != 0x80 || (s[3] & 0xc0) != 0x80)
            return 0;
        *cp = ((c & 0x07) << 18) | ((s[1] & 0x3f) << 12) |
              ((s[2] & 0x3f) << 6) | (s[3] & 0x3f);
        return 4;
    }
    return 0;
}

const Glyph *font_find(const Font *f, uint32_t cp) {
    int lo = 0, hi = f->n - 1;
    while (lo <= hi) {
        int m = (lo + hi) >> 1;
        if (f->g[m].cp == cp) return &f->g[m];
        if (f->g[m].cp < cp) lo = m + 1; else hi = m - 1;
    }
    return NULL;
}

int text_width(const Font *f, const char *s) {
    int w = 0;
    while (*s) {
        uint32_t cp;
        int n = utf8_decode(s, &cp);
        if (!n) { s++; continue; }
        s += n;
        const Glyph *g = font_find(f, cp);
        if (g) w += g->adv;
        else   w += f->px_size / 2;
    }
    return w;
}

/* Alpha-blend a single glyph's alpha8 bitmap onto a premultiplied-ARGB target.
 * For simplicity we treat the destination as already premultiplied-or-opaque
 * background and blend FG color modulated by glyph alpha as src-over. */
void draw_glyph(uint32_t *px, int sw, int sh, int x, int y,
                const Font *f, const Glyph *g, uint32_t fg) {
    if (!g || g->w <= 0 || g->h <= 0) return;
    int gx = x + g->bx;
    int gy = y - g->by;     /* y is baseline; bitmap top is baseline - by */
    const uint8_t *src = f->px + g->px_off;
    uint8_t fr = (fg >> 16) & 0xff, fg_g = (fg >> 8) & 0xff, fb = fg & 0xff;
    uint8_t fa = (fg >> 24) & 0xff;
    for (int j = 0; j < g->h; j++) {
        int yy = gy + j;
        if (yy < 0 || yy >= sh) continue;
        for (int i = 0; i < g->w; i++) {
            int xx = gx + i;
            if (xx < 0 || xx >= sw) continue;
            uint8_t a = src[j * g->w + i];
            if (!a) continue;
            uint32_t na = (a * fa) / 255;
            uint32_t d = px[yy * sw + xx];
            uint8_t dr = (d >> 16) & 0xff, dg = (d >> 8) & 0xff, db = d & 0xff;
            uint8_t da = (d >> 24) & 0xff;
            uint32_t inv = 255 - na;
            uint8_t or_ = (fr * na + dr * inv) / 255;
            uint8_t og  = (fg_g * na + dg * inv) / 255;
            uint8_t ob  = (fb * na + db * inv) / 255;
            uint8_t oa  = na + (da * inv) / 255;
            px[yy * sw + xx] = ((uint32_t)oa << 24) | ((uint32_t)or_ << 16)
                             | ((uint32_t)og << 8)  | ob;
        }
    }
}

void draw_text(uint32_t *px, int sw, int sh, int x, int y,
               const Font *f, const char *s, uint32_t fg) {
    int pen_x = x;
    int baseline = y + f->baseline;
    while (*s) {
        uint32_t cp;
        int n = utf8_decode(s, &cp);
        if (!n) { s++; continue; }
        s += n;
        const Glyph *g = font_find(f, cp);
        if (g) {
            draw_glyph(px, sw, sh, pen_x, baseline, f, g, fg);
            pen_x += g->adv;
        } else {
            pen_x += f->px_size / 2;
        }
    }
}

