/* Bar widget. Mirrors dwlb + dwlb-status + dwlb-leftstatus visual layout.
 *
 *   [logo HH:MM Mon 23]  [tag1 tag2 ...] window title   [disk cpu temp / bat vol vpn wifi]
 *
 * Tags state is pushed via `twlctl bar tags <occ> <act> <urg>`. Non-interactive. */

#include "twl.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *const TAG_LABEL[MAX_TAGS] = TAG_LABELS;

/* Nerd-font icon codepoints used here (kept in sync with tools/bake.c). */
#define I_DISK     0xf02ca
#define I_CPU      0xf4bc
#define I_MEM      0xf035b
#define I_TEMP     0xf0238
#define I_BAT_FULL  0xf240
#define I_BAT_75    0xf241
#define I_BAT_50    0xf242
#define I_BAT_25    0xf243
#define I_BAT_EMPTY 0xf244
#define I_BAT_CHG  0xf0084
#define I_WIFI_OFF 0xf092b
#define I_VPN_ON     0x25cf
#define I_VPN_STALE  0x25b2
#define I_VPN_OFF    0x25cb
#define I_LOGO       0xf32e
static const uint32_t I_WIFI[4] = { 0xf091f, 0xf0922, 0xf0925, 0xf0928 };

static int icon_bat(int pct, int chg) {
    if (chg) return I_BAT_CHG;
    if (pct > 87) return I_BAT_FULL;
    if (pct > 62) return I_BAT_75;
    if (pct > 37) return I_BAT_50;
    if (pct > 12) return I_BAT_25;
    return I_BAT_EMPTY;
}

/* Helpers to draw one glyph (as a "text fragment"). Used to compose runs. */
static int draw_cp(uint32_t *px, int sw, int sh, int x, int y,
                   const Font *f, uint32_t cp, uint32_t fg) {
    const Glyph *g = font_find(f, cp);
    if (!g) return 0;
    draw_glyph(px, sw, sh, x, y + f->baseline, f, g, fg);
    return g->adv;
}
static int cp_width(const Font *f, uint32_t cp) {
    const Glyph *g = font_find(f, cp);
    return g ? g->adv : f->px_size / 2;
}

static int draw_run(uint32_t *px, int sw, int sh, int x, int y,
                    const Font *f, const char *s, uint32_t fg) {
    int w = text_width(f, s);
    draw_text(px, sw, sh, x, y, f, s, fg);
    return w;
}

/* ---------- left segment ---------- */
static int draw_left(uint32_t *px, int sw, int sh, int x, int y,
                     const Font *f, const char *time, const char *date) {
    int x0 = x;
    x += draw_cp(px, sw, sh, x, y, f, I_LOGO, BAR_FG);
    x += draw_run(px, sw, sh, x, y, f, "  ", BAR_FG);
    x += draw_run(px, sw, sh, x, y, f, time, BAR_FG);
    x += draw_run(px, sw, sh, x, y, f, "    ", BAR_FG);
    x += draw_run(px, sw, sh, x, y, f, date, BAR_DIM);
    return x - x0;
}

/* ---------- right segment (status) ---------- */
/* Build a list of (cp_or_text, color) tokens and render them. */
typedef enum { TK_GLYPH, TK_TEXT } TkKind;
typedef struct {
    TkKind   kind;
    uint32_t cp;
    char     text[32];
    uint32_t color;
} Token;

static int tokens_width(const Font *f, const Token *t, int n) {
    int w = 0;
    for (int i = 0; i < n; i++)
        w += (t[i].kind == TK_GLYPH) ? cp_width(f, t[i].cp) : text_width(f, t[i].text);
    return w;
}
static int draw_tokens(uint32_t *px, int sw, int sh, int x, int y,
                       const Font *f, const Token *t, int n) {
    int x0 = x;
    for (int i = 0; i < n; i++) {
        if (t[i].kind == TK_GLYPH) x += draw_cp(px, sw, sh, x, y, f, t[i].cp, t[i].color);
        else                        x += draw_run(px, sw, sh, x, y, f, t[i].text, t[i].color);
    }
    return x - x0;
}

static int build_right_tokens(Token *t, int max) {
    int n = 0;
    #define G(cp_, c_)  do { if (n < max) { t[n].kind = TK_GLYPH; t[n].cp = (cp_); t[n].color = (c_); n++; } } while (0)
    #define T(s_, c_)   do { if (n < max) { t[n].kind = TK_TEXT; snprintf(t[n].text, sizeof t[n].text, "%s", (s_)); t[n].color = (c_); n++; } } while (0)
    #define F(c_, ...)  do { if (n < max) { t[n].kind = TK_TEXT; snprintf(t[n].text, sizeof t[n].text, __VA_ARGS__); t[n].color = (c_); n++; } } while (0)
#if SHOW_DISK
    G(I_DISK, BAR_FG); F(BAR_FG, " %d%%   ", status.disk_pct);
#endif
#if SHOW_CPU
    G(I_CPU,  BAR_FG);
    F(BAR_FG, " %d.%d%%   ", status.cpu_t10 / 10, status.cpu_t10 % 10);
    if (status.cpu_temp >= 0) {
        G(I_TEMP, BAR_FG); F(BAR_FG, " %d°C   ", status.cpu_temp);
    }
#endif
#if SHOW_MEM
    if (status.mem_used_kb >= 0) {
        G(I_MEM, BAR_FG);
        int used_mb = status.mem_used_kb / 1024;
        if (used_mb > 999)
            F(BAR_FG, " %d.%dGB   ", used_mb / 1024, (used_mb % 1024) * 10 / 1024);
        else
            F(BAR_FG, " %dMB   ", used_mb);
    }
#endif
    T("/   ", BAR_DIM);
#if SHOW_BAT
    if (status.bat_pct >= 0) {
        G(icon_bat(status.bat_pct, status.bat_charging), BAR_FG);
        F(BAR_FG, " %d%%   ", status.bat_pct);
    }
#endif
#if SHOW_VPN
    {
        uint32_t c = status.vpn_state == 1 ? VPN_ON_FG
                   : status.vpn_state == 2 ? VPN_STALE_FG
                   :                          VPN_OFF_FG;
        uint32_t i = status.vpn_state == 1 ? I_VPN_ON
                   : status.vpn_state == 2 ? I_VPN_STALE
                   :                          I_VPN_OFF;
        G(i, c); T("   ", BAR_FG);
    }
#endif
#if SHOW_WIFI
    G(status.wifi_level < 0 ? I_WIFI_OFF : I_WIFI[status.wifi_level], BAR_FG);
    T(" ", BAR_FG);
#endif
    #undef G
    #undef T
    #undef F
    return n;
}

/* ---------- center: workspace tags ----------
 * Each cell spans the full bar height; active/urgent tags get a background
 * slab. Vacant tags hidden (dwlb's hide-vacant behavior). */
static int draw_tags(uint32_t *px, int sw, int sh, int x, int y,
                     const Font *f, Widget *w) {
    int x0 = x;
    if (!w->s.bar.have_tags) return 0;
    for (int i = 0; i < MAX_TAGS; i++) {
        uint32_t bit = 1u << i;
        int occ = (w->s.bar.tag_mask    & bit) != 0;
        int act = (w->s.bar.active_mask & bit) != 0;
        int urg = (w->s.bar.urgent_mask & bit) != 0;
        if (!occ && !act && !urg) continue;
        int tw = text_width(f, TAG_LABEL[i]);
        int cell_w = tw + 2 * BAR_TAG_PAD_X;
        uint32_t bg = urg ? BAR_URGENT_BG : (act ? BAR_ACTIVE_BG : 0);
        if (bg) fill_rect(px, sw, sh, x, 0, cell_w, BAR_HEIGHT, bg);
        draw_text(px, sw, sh, x + BAR_TAG_PAD_X, y, f, TAG_LABEL[i], BAR_FG);
        x += cell_w + BAR_TAG_GAP;
    }
    return x - x0;
}

/* FNV-1a over an arbitrary byte run, folded into a running accumulator. */
static uint32_t fnv1a(const void *data, size_t n, uint32_t h) {
    const uint8_t *p = data;
    while (n--) { h ^= *p++; h *= 16777619u; }
    return h;
}

/* ---------- main bar render ----------
 * Skip the full clear+blit when nothing displayed has changed since the last
 * frame. 1Hz status_tfd ticks dominate idle CPU; with seconds not in the time
 * format, most ticks produce an identical bar. Hash includes the formatted
 * time/date strings, the tag masks, the title, and the entire `status` struct
 * (all integer fields). Width/height are included so a configure event always
 * forces a redraw. */
void bar_render(Widget *w) {
    if (!w->configured || w->w <= 0 || w->h <= 0) return;
    const Font *f = &font_small;

    time_t tt = time(NULL); struct tm tm; localtime_r(&tt, &tm);
    char timebuf[32], datebuf[64];
    strftime(timebuf, sizeof timebuf, TIME_FMT, &tm);
    strftime(datebuf, sizeof datebuf, DATE_FMT, &tm);

    uint32_t hash = 2166136261u;
    hash = fnv1a(timebuf, strlen(timebuf), hash);
    hash = fnv1a(datebuf, strlen(datebuf), hash);
    hash = fnv1a(&w->s.bar.tag_mask,    sizeof w->s.bar.tag_mask,    hash);
    hash = fnv1a(&w->s.bar.active_mask, sizeof w->s.bar.active_mask, hash);
    hash = fnv1a(&w->s.bar.urgent_mask, sizeof w->s.bar.urgent_mask, hash);
    hash = fnv1a(&w->s.bar.have_tags,   sizeof w->s.bar.have_tags,   hash);
    hash = fnv1a(w->s.bar.title, strlen(w->s.bar.title), hash);
    hash = fnv1a(&status, sizeof status, hash);
    hash = fnv1a(&w->w, sizeof w->w, hash);
    hash = fnv1a(&w->h, sizeof w->h, hash);
    if (!hash) hash = 1;  /* reserve 0 as "no prior render" */
    if (hash == w->s.bar.render_hash) return;
    w->s.bar.render_hash = hash;

    widget_ensure_pool(w, 1);
    BufSlot *s = widget_free_slot(w);
    if (!s) return;

    clear_buf(s->px, w->w, w->h, BAR_BG);
    int y = (BAR_HEIGHT - f->line_h) / 2;
    int left_w = draw_left(s->px, w->w, w->h, BAR_PAD_X, y, f, timebuf, datebuf);

    Token toks[64];
    int n_tok = build_right_tokens(toks, 64);
    int right_w = tokens_width(f, toks, n_tok);
    int right_x = w->w - BAR_PAD_X - right_w;
    if (right_x < BAR_PAD_X + left_w + 8) right_x = BAR_PAD_X + left_w + 8;
    draw_tokens(s->px, w->w, w->h, right_x, y, f, toks, n_tok);

    int center_x = BAR_PAD_X + left_w + 16;
    (void)draw_tags(s->px, w->w, w->h, center_x, y, f, w);

    widget_attach(w, s, 0);
}

void bar_redraw_all(void) {
    for (int i = 0; i < MAX_WIDGETS; i++)
        if (widgets[i].kind == W_BAR) bar_render(&widgets[i]);
}

void bar_create_on(Output *o) {
    if (!o || o->bar) return;
    Widget *w = widget_alloc(W_BAR);
    if (!w) { msg("twl: no widget slot for bar"); return; }
    o->bar = w;
    widget_setup_surface(w, LAYER_TOP, "twl-bar", o);
    widget_set_size(w, 0, BAR_HEIGHT);
    widget_set_anchor(w, LS_ANCHOR_TOP | LS_ANCHOR_LEFT | LS_ANCHOR_RIGHT);
    widget_set_exclusive_zone(w, BAR_HEIGHT);
    widget_set_kbd_interactive(w, 0);
    wl_req(w->surface, SURFACE_REQ_COMMIT, NULL, 0, -1);
    /* Seed tag state from the Output's accumulator (preserved across a
     * potential teardown→recreate, e.g. monitor flap). */
    if (o->have_tags) {
        w->s.bar.tag_mask    = o->tag_mask;
        w->s.bar.active_mask = o->active_mask;
        w->s.bar.urgent_mask = o->urgent_mask;
        w->s.bar.have_tags   = 1;
    }
}

void bar_set_tags_on(Output *o, uint32_t mask, uint32_t active, uint32_t urgent) {
    if (!o) return;
    o->tag_mask = mask; o->active_mask = active; o->urgent_mask = urgent;
    o->have_tags = 1;
    if (o->bar) {
        o->bar->s.bar.tag_mask    = mask;
        o->bar->s.bar.active_mask = active;
        o->bar->s.bar.urgent_mask = urgent;
        o->bar->s.bar.have_tags   = 1;
        bar_render(o->bar);
    }
}
void bar_set_title_on(Output *o, const char *s) {
    if (!o || !o->bar) return;
    size_t l = strnlen(s, sizeof o->bar->s.bar.title - 1);
    memcpy(o->bar->s.bar.title, s, l);
    o->bar->s.bar.title[l] = 0;
    bar_render(o->bar);
}

void bar_set_tags(uint32_t mask, uint32_t active, uint32_t urgent) {
    for (int i = 0; i < MAX_OUTPUTS; i++)
        if (outputs[i].active) bar_set_tags_on(&outputs[i], mask, active, urgent);
}
void bar_set_title(const char *s) {
    for (int i = 0; i < MAX_OUTPUTS; i++)
        if (outputs[i].active) bar_set_title_on(&outputs[i], s);
}
