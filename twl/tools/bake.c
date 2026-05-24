/* Build-time glyph baker. Rasterizes a curated subset of FiraCode Nerd Font
 * (ASCII text + icons used by bar/HUD/menu) at two pixel sizes into alpha8
 * bitmaps, emits bake.h. Run once at build time; freetype is not linked into
 * the runtime daemon. */

#define _GNU_SOURCE
#include <ft2build.h>
#include FT_FREETYPE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Two sizes are baked. Tuned for 1920x1080 + the WS_BAR_* / WS_HUD_* knobs in
 * dwlarp/config.h. SMALL serves the bar (height 28px ⇒ glyph ~16px). LARGE
 * serves HUD button icons (44x44) and menu rows. */
#define SIZE_SMALL 14
#define SIZE_LARGE 22

/* Codepoints. Plain ASCII 32..126 always baked at both sizes for status text
 * and menu items. Icon set follows. */
/* Codepoints transcribed directly from dwlb-status.c UTF-8 byte literals.
 * The 0xF000-range icons are 3-byte UTF-8; the 0xF0xxx-range are 4-byte —
 * an earlier pass conflated the two and baked unmapped private-use slots. */
static const uint32_t ICONS[] = {
    /* Bar status icons — codepoints match dwlb-status.c exactly */
    0xf02ca,  /* I_DISK    nf-md-harddisk    "\xf3\xb0\x8b\x8a" */
    0xf4bc,   /* I_CPU     nf-fa-microchip   "\xef\x92\xbc"     */
    0xf035b,  /* I_MEM     nf-md-memory      "\xf3\xb0\x8d\x9b" */
    0xf0238,  /* I_TEMP    nf-md-fire        "\xf3\xb0\x88\xb8" */
    0xf240,   /* I_BAT_FULL   "\xef\x89\x80" */
    0xf241,   /* I_BAT_75     "\xef\x89\x81" */
    0xf242,   /* I_BAT_50     "\xef\x89\x82" */
    0xf243,   /* I_BAT_25     "\xef\x89\x83" */
    0xf244,   /* I_BAT_EMPTY  "\xef\x89\x84" */
    0xf0084,  /* I_BAT_CHG    "\xf3\xb0\x82\x84" */
    0xf057e,  /* I_VOL_HI     "\xf3\xb0\x95\xbe" */
    0xf057f,  /* I_VOL_LO     "\xf3\xb0\x95\xbf" */
    0xf075f,  /* I_VOL_OFF    "\xf3\xb0\x9d\x9f" */
    0xf092b,  /* I_WIFI_OFF   "\xf3\xb0\xa4\xab" */
    0xf091f,  /* WIFI[0]      "\xf3\xb0\xa4\x9f" */
    0xf0922,  /* WIFI[1]      "\xf3\xb0\xa4\xa2" */
    0xf0925,  /* WIFI[2]      "\xf3\xb0\xa4\xa5" */
    0xf0928,  /* WIFI[3]      "\xf3\xb0\xa4\xa8" */
    /* VPN geometric shapes — base Unicode (≠ Nerd Font), so ^fg() colors apply */
    0x25cf,   /* ● VPN ON     */
    0x25b2,   /* ▲ VPN STALE  */
    0x25cb,   /* ○ VPN OFF    */
    /* Left logo */
    0xf32e,   /* nf-linux-void */
    /* HUD button glyphs (from WS_HUD_BUTTONS) */
    0xf186,   /* moon          */
    0xf1f6,   /* bell-slash    */
    0xf023,   /* lock          */
    0xf132,   /* shield        */
    0xf293,   /* bluetooth     */
    0xf1eb,   /* wifi (signal) */
    0xf028,   /* volume up     */
    /* OSD / notification slider glyphs (volume / mic / brightness) */
    0xf026,   /* volume off    */
    0xf130,   /* microphone    */
    0xf131,   /* microphone-slash */
    0xf185,   /* sun (brightness) */
    0xf0eb,   /* lightbulb (notification fallback icon) */
    /* Powermenu (built-in `twlctl powermenu`) */
    0xf011,   /* power-off */
    0xf021,   /* refresh / reboot */
    0xf28d,   /* pause / hibernate */
    0xf08b,   /* sign-out / logout */
};
#define N_ICONS (int)(sizeof ICONS / sizeof *ICONS)

typedef struct {
    uint32_t cp;
    int      w, h;
    int      bx, by;   /* bearing: glyph top-left offset from pen+baseline */
    int      adv;      /* horizontal advance from pen */
    int      px_off;   /* offset into per-size pixel pool */
} Glyph;

typedef struct {
    int      px_size;
    int      line_h;       /* nominal line height */
    int      baseline;     /* baseline offset from line top */
    Glyph   *glyphs;
    int      n_glyphs;
    uint8_t *pixels;
    int      px_len;
    int      px_cap;
} Bake;

static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n);
    if (!q) { fprintf(stderr, "oom\n"); exit(1); }
    return q;
}

static void push_pixels(Bake *b, const uint8_t *src, int n) {
    if (b->px_len + n > b->px_cap) {
        b->px_cap = (b->px_len + n) * 2 + 1024;
        b->pixels = xrealloc(b->pixels, b->px_cap);
    }
    memcpy(b->pixels + b->px_len, src, n);
    b->px_len += n;
}

static int rasterize(FT_Face face, uint32_t cp, Bake *b) {
    FT_UInt gi = FT_Get_Char_Index(face, cp);
    if (!gi) return -1;
    if (FT_Load_Glyph(face, gi, FT_LOAD_DEFAULT)) return -1;
    if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL)) return -1;

    FT_GlyphSlot s = face->glyph;
    FT_Bitmap *bm = &s->bitmap;
    int w = bm->width, h = bm->rows;

    Glyph g = {
        .cp = cp,
        .w  = w, .h = h,
        .bx = s->bitmap_left,
        .by = s->bitmap_top,
        .adv = s->advance.x >> 6,
        .px_off = b->px_len,
    };

    /* Repack to tight w-byte rows (drop FT pitch padding). */
    if (w && h) {
        uint8_t *tmp = xrealloc(NULL, (size_t)w * h);
        for (int y = 0; y < h; y++)
            memcpy(tmp + y * w, bm->buffer + y * bm->pitch, w);
        push_pixels(b, tmp, w * h);
        free(tmp);
    }

    b->glyphs = xrealloc(b->glyphs, sizeof(Glyph) * (b->n_glyphs + 1));
    b->glyphs[b->n_glyphs++] = g;
    return 0;
}

/* Codepoint-sorted comparator for runtime binary search. */
static int cmp_glyph(const void *a, const void *b) {
    return (int)((const Glyph *)a)->cp - (int)((const Glyph *)b)->cp;
}

static void bake_size(FT_Library ft, const char *font_path, int px_size, Bake *b) {
    FT_Face face;
    if (FT_New_Face(ft, font_path, 0, &face)) {
        fprintf(stderr, "can't open %s\n", font_path); exit(1);
    }
    if (FT_Set_Pixel_Sizes(face, 0, px_size)) {
        fprintf(stderr, "set_pixel_sizes(%d) failed\n", px_size); exit(1);
    }
    b->px_size  = px_size;
    b->line_h   = face->size->metrics.height   >> 6;
    b->baseline = face->size->metrics.ascender >> 6;

    /* ASCII first, then icons. Dedup not needed (disjoint ranges). */
    for (uint32_t c = 32; c <= 126; c++) rasterize(face, c, b);
    for (int i = 0; i < N_ICONS; i++)    rasterize(face, ICONS[i], b);

    qsort(b->glyphs, b->n_glyphs, sizeof(Glyph), cmp_glyph);
    FT_Done_Face(face);
}

static void emit_bake(FILE *f, const char *tag, const Bake *b) {
    fprintf(f,
        "static const uint8_t font_%s_px[%d] = {\n   ", tag, b->px_len);
    for (int i = 0; i < b->px_len; i++) {
        fprintf(f, " 0x%02x,", b->pixels[i]);
        if ((i & 15) == 15) fprintf(f, "\n   ");
    }
    fprintf(f, "\n};\n\n");

    fprintf(f, "static const Glyph font_%s_g[%d] = {\n", tag, b->n_glyphs);
    for (int i = 0; i < b->n_glyphs; i++) {
        const Glyph *g = &b->glyphs[i];
        fprintf(f, "    { 0x%05x, %d, %d, %d, %d, %d, %d },\n",
                g->cp, g->w, g->h, g->bx, g->by, g->adv, g->px_off);
    }
    fprintf(f, "};\n\n");

    fprintf(f,
        "static const Font font_%s = {\n"
        "    .px_size  = %d,\n"
        "    .line_h   = %d,\n"
        "    .baseline = %d,\n"
        "    .n        = %d,\n"
        "    .g        = font_%s_g,\n"
        "    .px       = font_%s_px,\n"
        "};\n\n",
        tag, b->px_size, b->line_h, b->baseline, b->n_glyphs, tag, tag);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: bake <font.ttf> <out.h>\n");
        return 2;
    }
    const char *font_path = argv[1];
    const char *out_path  = argv[2];

    FT_Library ft;
    if (FT_Init_FreeType(&ft)) { fprintf(stderr, "ft init\n"); return 1; }

    Bake small = {0}, large = {0};
    bake_size(ft, font_path, SIZE_SMALL, &small);
    bake_size(ft, font_path, SIZE_LARGE, &large);

    FILE *f = fopen(out_path, "w");
    if (!f) { perror(out_path); return 1; }

    fprintf(f,
        "/* Auto-generated by tools/bake.c. Do not edit. */\n"
        "#ifndef TWL_BAKE_H\n"
        "#define TWL_BAKE_H\n"
        "#include <stdint.h>\n\n"
        "typedef struct {\n"
        "    uint32_t cp;\n"
        "    int16_t  w, h;\n"
        "    int16_t  bx, by;\n"
        "    int16_t  adv;\n"
        "    int32_t  px_off;\n"
        "} Glyph;\n\n"
        "typedef struct {\n"
        "    int            px_size;\n"
        "    int            line_h;\n"
        "    int            baseline;\n"
        "    int            n;\n"
        "    const Glyph   *g;\n"
        "    const uint8_t *px;\n"
        "} Font;\n\n");

    emit_bake(f, "small", &small);
    emit_bake(f, "large", &large);

    fprintf(f, "#endif\n");
    fclose(f);

    fprintf(stderr, "bake: %s small=%d glyphs/%d B  large=%d glyphs/%d B\n",
            out_path, small.n_glyphs, small.px_len,
                       large.n_glyphs, large.px_len);

    FT_Done_FreeType(ft);
    return 0;
}
