/* OSD / notification stack — replaces both mako (app notifications) and
 * dwl-osd (volume / brightness / mic sliders).
 *
 * One W_OSD widget; surface sized to hold MAX_OSDS stacked slabs vertically.
 * Active items pack to the top, inactive rows render transparent so the
 * unused area below is click-through. Replacement-by-id is the same idea as
 * mako's x-canonical-private-synchronous: a fresh post with a matching
 * `replace_id` overwrites the existing slot instead of stacking. */

#include "twl.h"

#include <stdio.h>
#include <string.h>

/* Vertical padding inside a slab (top + bottom) around the text block. */
#define OSD_SLAB_PAD_Y 24
/* Worst-case slab height: 1 summary line + OSD_MAX_BODY_LINES body lines +
 * progress band + padding. Surface is sized for the worst case so we never
 * have to renegotiate size with the compositor at runtime. */
#define OSD_PROG_BAND  (OSD_PROG_H + 8)
#define OSD_MAX_SLAB_H ((1 + OSD_MAX_BODY_LINES) * 17 + OSD_PROG_BAND + OSD_SLAB_PAD_Y)
#define OSD_TOTAL_H    (MAX_OSDS * OSD_MAX_SLAB_H + (MAX_OSDS - 1) * OSD_GAP)

extern void dbus_emit_closed(uint32_t id, uint32_t reason) __attribute__((weak));

int dnd_on = 0;

static void update_input_region(Widget *w);

/* Walk the prefix of `s` whose pixel width fits `budget`, returning byte count. */
static int prefix_fitting(const Font *f, const char *s, int budget) {
    int n = 0, w = 0;
    while (s[n]) {
        uint32_t cp; int k = utf8_decode(s + n, &cp);
        if (!k) break;
        const Glyph *g = font_find(f, cp);
        int gw = g ? g->adv : f->px_size / 2;
        if (w + gw > budget) break;
        n += k; w += gw;
    }
    return n;
}

/* Draw `s` at (x,y); if wider than max_w, truncate and append '…'. */
static void draw_elided(uint32_t *px, int sw, int sh, int x, int y,
                        const Font *f, const char *s, int max_w, uint32_t fg) {
    if (text_width(f, s) <= max_w) { draw_text(px, sw, sh, x, y, f, s, fg); return; }
    int ell_w = text_width(f, "\xe2\x80\xa6");
    int budget = max_w - ell_w;
    if (budget < 0) budget = 0;
    int n = prefix_fitting(f, s, budget);
    char buf[OSD_BODY_MAX + 8];
    if (n > (int)sizeof buf - 4) n = sizeof buf - 4;
    memcpy(buf, s, n);
    memcpy(buf + n, "\xe2\x80\xa6", 3);
    buf[n + 3] = 0;
    draw_text(px, sw, sh, x, y, f, buf, fg);
}

/* Word-wrap `s` into at most `max_lines` lines, each <= max_w pixels.
 * Final line gets an ellipsis only if input doesn't fit within max_lines. */
static int wrap_body(const Font *f, const char *s, int max_w, int max_lines,
                     char out[][OSD_BODY_MAX]) {
    int line = 0;
    int sp_w = text_width(f, " ");
    char cur[OSD_BODY_MAX] = ""; int cur_len = 0, cur_w = 0;

    while (*s && line < max_lines) {
        while (*s == ' ' || *s == '\t' || *s == '\n') s++;
        if (!*s) break;
        const char *we = s;
        while (*we && *we != ' ') we++;
        int wl = (int)(we - s);
        if (wl >= (int)sizeof cur) wl = sizeof cur - 1;
        char word[OSD_BODY_MAX];
        memcpy(word, s, wl); word[wl] = 0;
        int ww = text_width(f, word);
        int add_sp = cur_len > 0 ? sp_w : 0;
        if (cur_w + add_sp + ww > max_w) {
            if (cur_len == 0) {
                int cut = prefix_fitting(f, word, max_w);
                if (cut == 0) cut = 1;
                memcpy(out[line], word, cut); out[line][cut] = 0;
                line++;
                s += cut;
                continue;
            }
            memcpy(out[line], cur, cur_len); out[line][cur_len] = 0;
            line++; cur[0] = 0; cur_len = 0; cur_w = 0;
            continue;
        }
        if (add_sp) { cur[cur_len++] = ' '; cur[cur_len] = 0; cur_w += sp_w; }
        memcpy(cur + cur_len, word, wl); cur_len += wl; cur[cur_len] = 0;
        cur_w += ww;
        s = we;
    }
    if (cur_len > 0 && line < max_lines) {
        memcpy(out[line], cur, cur_len); out[line][cur_len] = 0;
        line++;
    }
    if (*s && line > 0) {
        char *last = out[line - 1];
        int ell_w = text_width(f, "\xe2\x80\xa6");
        int lw = text_width(f, last);
        while (lw + ell_w > max_w) {
            int len = (int)strlen(last);
            if (!len) break;
            int back = 1;
            while (back < len && (last[len - back] & 0xc0) == 0x80) back++;
            last[len - back] = 0;
            lw = text_width(f, last);
        }
        size_t ll = strlen(last);
        if (ll + 3 < OSD_BODY_MAX) { memcpy(last + ll, "\xe2\x80\xa6", 3); last[ll + 3] = 0; }
    }
    return line;
}

static int find_replace(Widget *w, uint32_t rid) {
    if (!rid) return -1;
    for (int i = 0; i < MAX_OSDS; i++)
        if (w->s.osd.items[i].active && w->s.osd.items[i].replace_id == rid)
            return i;
    return -1;
}
static int find_free(Widget *w) {
    for (int i = 0; i < MAX_OSDS; i++)
        if (!w->s.osd.items[i].active) return i;
    return -1;
}
/* Evict the soonest-to-expire non-critical slot. If the stack is full of
 * critical-urgency items, evict the soonest-to-expire critical only when the
 * incoming notification is itself critical — otherwise refuse (return -1) so
 * we never silently drop a critical alert in favor of a normal/low one.
 * Sticky-critical (expires_at_ms == 0) is never displaced. */
static int evict(Widget *w, int incoming_urgency) {
    int oldest = -1;
    int64_t best = INT64_MAX;
    for (int i = 0; i < MAX_OSDS; i++) {
        if (!w->s.osd.items[i].active || w->s.osd.items[i].urgency >= 2) continue;
        int64_t e = w->s.osd.items[i].expires_at_ms;
        if (e && e < best) { best = e; oldest = i; }
    }
    if (oldest < 0 && incoming_urgency >= 2) {
        for (int i = 0; i < MAX_OSDS; i++) {
            if (!w->s.osd.items[i].active) continue;
            int64_t e = w->s.osd.items[i].expires_at_ms;
            if (e && e < best) { best = e; oldest = i; }
        }
    }
    if (oldest < 0) return -1;
    uint32_t evicted_id = w->s.osd.items[oldest].replace_id;
    w->s.osd.items[oldest].active = 0;
    if (dbus_emit_closed) dbus_emit_closed(evicted_id, 2 /*dismissed*/);
    return oldest;
}

/* Pack active items into a contiguous prefix preserving insertion order. */
static void pack(Widget *w) {
    Osd tmp[MAX_OSDS];
    int n = 0;
    for (int i = 0; i < MAX_OSDS; i++)
        if (w->s.osd.items[i].active) tmp[n++] = w->s.osd.items[i];
    for (int i = 0; i < n; i++) w->s.osd.items[i] = tmp[i];
    for (int i = n; i < MAX_OSDS; i++) w->s.osd.items[i].active = 0;
}

/* Final tx (post-icon) for body/summary text in a slab. */
static int slab_text_x(const Osd *o) {
    int tx = OSD_PAD_X;
    if (o->icon_cp) {
        const Glyph *g = font_find(&font_large, o->icon_cp);
        if (g) tx += g->adv + OSD_ICON_GAP;
    }
    return tx;
}

static int slab_height_for(int nbody, int has_progress) {
    int text_h = (1 + nbody) * font_small.line_h;
    int prog_h = has_progress ? OSD_PROG_H + 8 : 0;
    int h = text_h + prog_h + OSD_SLAB_PAD_Y;
    if (h < OSD_SLAB_H) h = OSD_SLAB_H;
    return h;
}

void osd_render(Widget *w) {
    if (!w->configured) return;
    int has_items = w->s.osd.items[0].active;
    if (!has_items && !w->s.osd.has_pixels) return;

    /* Empty transition: attach a fully transparent buffer rather than NULL.
     * NULL-attach unmaps the layer surface, which makes wlroots destroy every
     * pending entry in configure_list. Any configure already in flight to us
     * is then unackable — when we receive it and reply with ack_configure,
     * wlroots fires the fatal "wrong configure serial" protocol error. Keeping
     * the surface mapped with a transparent buffer keeps configure_list
     * consistent. Frame callback drives a one-shot SHM-pool free in
     * on_frame_done (hud.c) so the ~2.8 MB pool isn't kept around between
     * notification bursts. */
    if (!has_items) {
        update_input_region(w);
        widget_ensure_pool(w, 2);
        BufSlot *s = widget_free_slot(w);
        if (!s) return;
        memset(s->px, 0, (size_t)w->w * w->h * 4);
        widget_attach(w, s, 1);
        w->s.osd.has_pixels = 0;
        w->want_pool_free = 1;
        return;
    }

    const Font *f = &font_small;

    /* Pass 1: wrap bodies, compute per-slab height and Y, sum total. */
    static char wrapped[MAX_OSDS][OSD_MAX_BODY_LINES][OSD_BODY_MAX];
    int nbody[MAX_OSDS]   = {0};
    int item_y[MAX_OSDS]  = {0};
    int total = 0, n_active = 0;
    for (int i = 0; i < MAX_OSDS; i++) {
        Osd *o = &w->s.osd.items[i];
        if (!o->active) break;
        int tx = slab_text_x(o);
        int body_w = OSD_W - OSD_PAD_X - tx;
        if (body_w < 0) body_w = 0;
        if (o->body[0])
            nbody[i] = wrap_body(f, o->body, body_w, OSD_MAX_BODY_LINES, wrapped[i]);
        o->h = slab_height_for(nbody[i], o->progress >= 0);
        if (n_active > 0) total += OSD_GAP;
        item_y[i] = total;
        total += o->h;
        n_active++;
    }

    /* Per-item heights are now known — input region must be set from them,
     * not from the stale values that existed when osd_post called us. */
    update_input_region(w);
    (void)total;

    /* Two slots: when the compositor is still holding the just-attached
     * buffer (e.g. click-dismiss right after a post), having a free slot
     * lets us re-render and clear without waiting on buffer.release. */
    widget_ensure_pool(w, 2);
    BufSlot *s = widget_free_slot(w);
    if (!s) return;
    int W = w->w, H = w->h;
    memset(s->px, 0, (size_t)W * H * 4);

    for (int i = 0; i < n_active; i++) {
        Osd *o = &w->s.osd.items[i];
        int y  = item_y[i];
        int sh = o->h;
        /* style: 0=normal teal, 1=muted red (audio mute), 2=warn yellow (low bat). */
        uint32_t bg = o->muted == 2 ? OSD_BG_WARN
                    : o->muted == 1 ? OSD_BG_MUTE  : OSD_BG;
        uint32_t fg = o->muted == 2 ? OSD_FG_WARN
                    : o->muted == 1 ? OSD_FG_MUTE  : OSD_FG;
        uint32_t bd = o->urgency >= 2 ? OSD_BORDER_CRIT
                    : o->muted == 2   ? OSD_BORDER_WARN
                    : o->muted == 1   ? OSD_BORDER_MUTE
                    :                   OSD_BORDER;
        uint32_t pfg = o->muted == 2 ? OSD_PROG_FG_WARN
                     : o->muted == 1 ? OSD_PROG_FG_MUTE : OSD_PROG_FG;

        fill_rect(s->px, W, H, 0,   y,         W, sh, bg);
        fill_rect(s->px, W, H, 0,   y,         W, 2,  bd);
        fill_rect(s->px, W, H, 0,   y+sh-2,    W, 2,  bd);
        fill_rect(s->px, W, H, 0,   y,         2, sh, bd);
        fill_rect(s->px, W, H, W-2, y,         2, sh, bd);

        char pct_buf[16] = "";
        int pct_w = 0;
        if (o->progress >= 0) {
            snprintf(pct_buf, sizeof pct_buf, "%d%%", o->progress);
            pct_w = text_width(f, pct_buf);
        }

        int tx = slab_text_x(o);
        int prog_band = (o->progress >= 0) ? OSD_PROG_H + 8 : 0;
        int text_zone_h = sh - prog_band;
        int body_w = W - OSD_PAD_X - tx;
        if (body_w < 0) body_w = 0;

        if (o->icon_cp) {
            const Glyph *g = font_find(&font_large, o->icon_cp);
            if (g) {
                int gy = y + (text_zone_h - g->h) / 2 + g->by;
                draw_glyph(s->px, W, H, OSD_PAD_X - g->bx, gy, &font_large, g, fg);
            }
        }

        if (nbody[i] > 0) {
            int total_lines = 1 + nbody[i];
            int ty = y + (text_zone_h - total_lines * f->line_h) / 2;
            draw_elided(s->px, W, H, tx, ty, f, o->summary, body_w, fg);
            for (int li = 0; li < nbody[i]; li++)
                draw_text(s->px, W, H, tx, ty + (li + 1) * f->line_h, f, wrapped[i][li], fg);
        } else {
            int ty = y + (text_zone_h - f->line_h) / 2;
            int sum_w = body_w - (pct_w ? pct_w + OSD_ICON_GAP : 0);
            if (sum_w < 0) sum_w = 0;
            draw_elided(s->px, W, H, tx, ty, f, o->summary, sum_w, fg);
            if (pct_w) {
                int px_ = W - OSD_PAD_X - pct_w;
                draw_text(s->px, W, H, px_, ty, f, pct_buf, fg);
            }
        }

        if (o->progress >= 0) {
            int by = y + sh - 2 - OSD_PROG_H - 4;
            int bx = OSD_PAD_X;
            int bw = W - 2 * OSD_PAD_X;
            fill_rect(s->px, W, H, bx, by, bw, OSD_PROG_H, OSD_PROG_TRACK_BG);
            int pmax = o->progress > 100 ? o->progress : 100;
            int pw = bw * o->progress / pmax;
            if (pw > bw) pw = bw;
            if (pw > 0) fill_rect(s->px, W, H, bx, by, pw, OSD_PROG_H, pfg);
        }
    }
    w->s.osd.has_pixels = 1;
    widget_attach(w, s, 0);
}

/* Create an OSD widget anchored to `o`. Layer-surface output is fixed at
 * creation time, so focus-follows-output requires destroy+recreate. */
static Widget *osd_make_on(Output *o) {
    Widget *w = widget_alloc(W_OSD);
    if (!w) { msg("twl: no widget slot for OSD"); return NULL; }
    w->w = OSD_W;
    w->h = OSD_TOTAL_H;
    widget_setup_surface(w, LAYER_OVERLAY, "twl-osd", o);
    widget_set_size(w, w->w, w->h);
    /* Anchor TOP only → compositor centers horizontally. */
    widget_set_anchor(w, LS_ANCHOR_TOP);
    widget_set_margin(w, OSD_TOP_MARGIN, 0, 0, 0);
    widget_set_exclusive_zone(w, -1);
    widget_set_kbd_interactive(w, 0);
    /* Empty input region: OSD never steals pointer events. */
    widget_set_input_region_rect(w, 0, 0, 0, 0);
    wl_req(w->surface, SURFACE_REQ_COMMIT, NULL, 0, -1);
    return w;
}

/* Return the OSD widget anchored to the focused output, migrating it (and
 * carrying active slabs across) if focus has moved since the last post. */
static Widget *osd_ensure(void) {
    Output *target = focused_output;
    if (!target) {
        /* Fall back to first connected output. */
        for (int i = 0; i < MAX_OUTPUTS; i++)
            if (outputs[i].active) { target = &outputs[i]; break; }
    }
    if (!target) return NULL;
    Widget *w = widget_first(W_OSD);
    if (w && w->output == target) return w;
    if (w) {
        /* Migrate: snapshot items, tear down, rebuild on the new output. */
        Osd snap[MAX_OSDS];
        memcpy(snap, w->s.osd.items, sizeof snap);
        uint32_t next_id = w->s.osd.next_id;
        widget_destroy(w);
        w = osd_make_on(target);
        if (!w) return NULL;
        memcpy(w->s.osd.items, snap, sizeof snap);
        w->s.osd.next_id = next_id;
        /* Re-render happens at caller (osd_post → osd_render). */
        return w;
    }
    return osd_make_on(target);
}

uint32_t osd_post(uint32_t replace_id, const char *summary, const char *body,
                  uint32_t icon_cp, int progress, int urgency, int muted,
                  int timeout_ms) {
    Widget *w = osd_ensure();
    if (!w) return 0;

    int slot = find_replace(w, replace_id);
    if (slot < 0) slot = find_free(w);
    if (slot < 0) slot = evict(w, urgency);
    if (slot < 0) return 0;  /* refused — incoming would have displaced a critical */

    Osd *o = &w->s.osd.items[slot];
    memset(o, 0, sizeof *o);
    o->active = 1;
    o->replace_id = replace_id ? replace_id : ++w->s.osd.next_id;
    if (summary) snprintf(o->summary, sizeof o->summary, "%s", summary);
    if (body)    snprintf(o->body,    sizeof o->body,    "%s", body);
    o->icon_cp  = icon_cp;
    o->progress = progress;
    o->urgency  = urgency;
    o->muted    = muted;
    if (timeout_ms < 0) {
        o->expires_at_ms = now_ms()
            + (urgency == 0 ? OSD_TIMEOUT_LOW : OSD_TIMEOUT_NORMAL);
    } else if (timeout_ms == 0) {
        o->expires_at_ms = 0;  /* sticky */
    } else {
        o->expires_at_ms = now_ms() + timeout_ms;
    }

    pack(w);
    osd_render(w);  /* refreshes input region from computed heights */
    return o->replace_id;
}

void osd_close(uint32_t id) {
    Widget *w = widget_first(W_OSD);
    if (!w) return;
    int slot = find_replace(w, id);
    if (slot < 0) return;
    w->s.osd.items[slot].active = 0;
    pack(w);
    osd_render(w);  /* refreshes input region from computed heights */
    if (dbus_emit_closed) dbus_emit_closed(id, 3 /*closed by call*/);
}

void osd_close_all(void) {
    Widget *w = widget_first(W_OSD);
    if (!w) return;
    for (int i = 0; i < MAX_OSDS; i++) {
        if (w->s.osd.items[i].active && dbus_emit_closed)
            dbus_emit_closed(w->s.osd.items[i].replace_id, 3);
        w->s.osd.items[i].active = 0;
    }
    osd_render(w);
}

int osd_check_expiry(int64_t now) {
    Widget *w = widget_first(W_OSD);
    if (!w) return -1;
    int next = -1, redraw = 0;
    for (int i = 0; i < MAX_OSDS; i++) {
        Osd *o = &w->s.osd.items[i];
        if (!o->active || !o->expires_at_ms) continue;
        int64_t left = o->expires_at_ms - now;
        if (left <= 0) {
            uint32_t id = o->replace_id;
            o->active = 0;
            redraw = 1;
            if (dbus_emit_closed) dbus_emit_closed(id, 1 /*expired*/);
        } else if (next < 0 || left < next) next = (int)left;
    }
    if (redraw) { pack(w); osd_render(w); }
    return next;
}

/* Y → slab index, or -1 if click is in a gap / past the stack. Walks per-item
 * heights because slabs are now variable-height. */
static int slab_at(Widget *w, int y) {
    if (y < 0) return -1;
    int off = 0;
    for (int i = 0; i < MAX_OSDS; i++) {
        Osd *o = &w->s.osd.items[i];
        if (!o->active) break;
        int sh = o->h > 0 ? o->h : OSD_SLAB_H;
        if (y >= off && y < off + sh) return i;
        off += sh + OSD_GAP;
    }
    return -1;
}

/* Refresh the surface's input region: hit-testable only over active slabs.
 * Composed of one rect per active item so the inter-slab gaps stay
 * click-through (don't steal clicks from windows below). */
static void update_input_region(Widget *w) {
    int total = 0, n = 0;
    for (int i = 0; i < MAX_OSDS; i++) {
        Osd *o = &w->s.osd.items[i];
        if (!o->active) break;
        if (n > 0) total += OSD_GAP;
        total += o->h > 0 ? o->h : OSD_SLAB_H;
        n++;
    }
    if (n == 0) {
        widget_set_input_region_rect(w, 0, 0, 0, 0);
        return;
    }
    /* Single contiguous rect covering all active slabs — simpler than
     * one-region-per-slab and clicks in gaps just dismiss the slab above. */
    widget_set_input_region_rect(w, 0, 0, w->w, total);
}

void osd_on_click(Widget *w, int x, int y) {
    (void)x;
    int idx = slab_at(w, y);
    if (idx < 0) return;
    if (!w->s.osd.items[idx].active) return;
    uint32_t id = w->s.osd.items[idx].replace_id;
    osd_close(id);
}

