/* HUD button panel — ws-hud clone. Hover-trigger from a thin top strip; slides
 * down into a button grid; clicks fire shell commands; tri-state probes color
 * toggles green/yellow/off. Animation is wl_surface.frame paced. */

#include "twl.h"

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int type;
    const char *a, *b, *c;
    const char *state_cmd;
    uint32_t icon;
} Btn;

static const Btn BTNS[] = HUD_BUTTONS_INIT;
#define N_BTN ((int)(sizeof BTNS / sizeof *BTNS))

#define WIDGET_W  (N_BTN * HUD_BTN_W + (N_BTN - 1) * HUD_BTN_GAP + 2 * HUD_PAD)
#define BTN_Y     (BAR_HEIGHT - HUD_BAR_OVERLAP)
#define WIDGET_H  (BTN_Y + HUD_BTN_H + HUD_PAD)

static int64_t last_btn_ms;
static int64_t last_state_probe_ms;

/* In-flight async probes. Each entry maps a child pid → button index; cleared
 * when hud_on_sigchld reaps the child via waitpid(WNOHANG). */
static pid_t probe_pid[MAX_BUTTONS];

static int btn_x(int i) { return HUD_PAD + i * (HUD_BTN_W + HUD_BTN_GAP); }

static int hit(int x, int y, int oy) {
    int by = BTN_Y + oy;
    if (y < by || y >= by + HUD_BTN_H) return -1;
    for (int i = 0; i < N_BTN; i++) {
        int bx = btn_x(i);
        if (x >= bx && x < bx + HUD_BTN_W) return i;
    }
    return -1;
}

static void run_cmd(const char *cmd) {
    if (!cmd) return;
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); close(devnull); }
        execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
        _exit(127);
    }
}

/* Spawn state_cmd for every button that has one. Fire-and-forget from the
 * parent's perspective — children are reaped later in hud_on_sigchld, which
 * waits on SIGCHLD via signalfd. Buttons whose previous probe is still
 * in-flight are skipped (no stacking). Probes are system-state queries that
 * apply uniformly to every HUD instance, so we run one set of children and
 * fan the result out on reap. */
void hud_probe_states(void) {
    if (!widget_first(W_HUD)) return;
    for (int i = 0; i < N_BTN; i++) {
        if (!BTNS[i].state_cmd) continue;
        if (probe_pid[i] > 0) continue;
        pid_t pid = fork();
        if (pid == 0) {
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); close(devnull); }
            execl("/bin/sh", "sh", "-c", BTNS[i].state_cmd, (char *)NULL);
            _exit(127);
        }
        if (pid > 0) probe_pid[i] = pid;
    }
    last_state_probe_ms = now_ms();
}

/* Called from twl.c's signalfd handler. Drains *all* reapable children: probe
 * pids get matched and update btn_state on every HUD widget; run_cmd's
 * fire-and-forget children are reaped silently (otherwise they'd zombie since
 * we no longer rely on SIG_IGN auto-reap). */
void hud_on_sigchld(void) {
    int any = 0;
    for (;;) {
        int st;
        pid_t pid = waitpid(-1, &st, WNOHANG);
        if (pid <= 0) break;
        for (int i = 0; i < N_BTN; i++) {
            if (probe_pid[i] != pid) continue;
            probe_pid[i] = 0;
            int rc = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
            int s = (rc == 0) ? 1 : (rc == 2) ? 2 : 0;
            for (int j = 0; j < MAX_WIDGETS; j++) {
                if (widgets[j].kind != W_HUD) continue;
                widgets[j].s.hud.btn_state[i] = s;
            }
            any = 1;
            break;
        }
    }
    if (!any) return;
    for (int j = 0; j < MAX_WIDGETS; j++) {
        Widget *h = &widgets[j];
        if (h->kind != W_HUD || !h->s.hud.visible) continue;
        hud_render(h, (int)h->s.hud.cur_oy);
    }
}

void hud_render(Widget *w, int oy) {
    widget_ensure_pool(w, 2);
    BufSlot *s = widget_free_slot(w);
    if (!s) return;
    int W = WIDGET_W, H = WIDGET_H;

    /* transparent base */
    memset(s->px, 0, W * H * 4);

    /* body anchored at bar's bottom, collapses with oy */
    int body_y = BAR_HEIGHT;
    int body_bot = WIDGET_H + oy;
    int body_h = body_bot - body_y;
    if (body_h > 0) {
        fill_rect(s->px, W, H, 0,           body_y,             W,         body_h,   HUD_BG);
        fill_rect(s->px, W, H, 0,           body_y,             HUD_OUTER, body_h,   HUD_FG);
        fill_rect(s->px, W, H, W-HUD_OUTER, body_y,             HUD_OUTER, body_h,   HUD_FG);
        fill_rect(s->px, W, H, 0,           body_bot-HUD_OUTER, W,         HUD_OUTER, HUD_FG);
    }

    int by = BTN_Y + oy;
    int t = HUD_BTN_BORDER;
    for (int i = 0; i < N_BTN; i++) {
        int bx = btn_x(i);
        if (w->s.hud.held_btn == i)
            fill_rect(s->px, W, H, bx, by, HUD_BTN_W, HUD_BTN_H, HUD_HOLD);
        else if (BTNS[i].type == 1 && w->s.hud.btn_state[i] == 1)
            fill_rect(s->px, W, H, bx, by, HUD_BTN_W, HUD_BTN_H, HUD_ON);
        else if (BTNS[i].type == 1 && w->s.hud.btn_state[i] == 2)
            fill_rect(s->px, W, H, bx, by, HUD_BTN_W, HUD_BTN_H, HUD_WARN);
        fill_rect(s->px, W, H, bx,                 by,                  HUD_BTN_W, t,         HUD_FG);
        fill_rect(s->px, W, H, bx,                 by + HUD_BTN_H - t,  HUD_BTN_W, t,         HUD_FG);
        fill_rect(s->px, W, H, bx,                 by,                  t,         HUD_BTN_H, HUD_FG);
        fill_rect(s->px, W, H, bx + HUD_BTN_W - t, by,                  t,         HUD_BTN_H, HUD_FG);
        /* center icon */
        const Glyph *g = font_find(&font_large, BTNS[i].icon);
        if (g) {
            /* draw_glyph treats (gx,gy) as the baseline pen position. bearing
             * x/y place the bitmap relative to the pen — solve for pen so the
             * glyph bitmap centers in the button. */
            int gx = bx + (HUD_BTN_W - g->w) / 2 - g->bx;
            int gy = by + (HUD_BTN_H - g->h) / 2 + g->by;
            draw_glyph(s->px, W, H, gx, gy, &font_large, g, HUD_ICON);
        }
    }
    widget_attach(w, s, w->s.hud.animating);
}

static void press(Widget *w, int idx) {
    const Btn *b = &BTNS[idx];
    if (b->type == 0) { run_cmd(b->a); return; }
    int *st = &w->s.hud.btn_state[idx];
    if (*st == 1)      { run_cmd(b->b); *st = 0; }
    else if (*st == 2) { run_cmd(b->c ? b->c : b->a); }
    else               { run_cmd(b->a); *st = 1; }
}

void hud_tick(Widget *w, int64_t now) {
    if (!w->s.hud.animating) return;
    double dt = (double)(now - w->s.hud.anim_last_ms);
    w->s.hud.anim_last_ms = now;
    double k = 1.0 - exp(-dt / HUD_ANIM_TAU_MS);
    w->s.hud.cur_oy += (w->s.hud.target_oy - w->s.hud.cur_oy) * k;
    if (fabs(w->s.hud.cur_oy - w->s.hud.target_oy) < HUD_ANIM_EPSILON) {
        w->s.hud.cur_oy = w->s.hud.target_oy;
        w->s.hud.animating = 0;
        if (w->s.hud.target_oy <= -WIDGET_H) {
            w->s.hud.visible = 0;
            widget_set_input_region(w, w->s.hud.region_trigger);
            BufSlot *s = widget_free_slot(w);
            if (s) {
                memset(s->px, 0, WIDGET_W * WIDGET_H * 4);
                /* Frame callback drives the SHM-pool free in on_frame_done,
                 * dropping ~190 KB whenever the HUD is fully hidden. */
                widget_attach(w, s, 1);
                w->want_pool_free = 1;
            }
            return;
        }
    }
    hud_render(w, (int)w->s.hud.cur_oy);
}

void on_frame_done(Widget *w, uint32_t cb_id) {
    if (w->frame_cb == cb_id) w->frame_cb = 0;
    if (w->kind == W_HUD && w->s.hud.animating) {
        hud_tick(w, now_ms());
        return;
    }
    /* Static / idle surfaces (wallpaper after first paint, HUD when fully
     * hidden, OSD when empty) opt into one-shot SHM release via this flag.
     * widget_ensure_pool clears it if a new render reclaims the pool before
     * frame.done lands, so we never yank an in-use buffer. The compositor
     * keeps its texture reference, so visible content stays on screen. */
    if (w->want_pool_free) {
        w->want_pool_free = 0;
        widget_free_pool(w);
    }
}

static void show(Widget *w) {
    int was_hidden = !w->s.hud.visible;
    w->s.hud.visible = 1;
    w->s.hud.target_oy = 0;
    w->s.hud.animating = 1;
    w->s.hud.anim_last_ms = now_ms();
    if (was_hidden) {
        widget_set_input_region(w, w->s.hud.region_full);
        hud_render(w, (int)w->s.hud.cur_oy);
        if (now_ms() - last_state_probe_ms > 1500) {
            hud_probe_states();
        }
    }
}
static void hide(Widget *w) {
    if (w->s.hud.target_oy <= -WIDGET_H && !w->s.hud.animating) return;
    w->s.hud.target_oy = -WIDGET_H;
    w->s.hud.animating = 1;
    w->s.hud.anim_last_ms = now_ms();
    if (!w->frame_cb) hud_render(w, (int)w->s.hud.cur_oy);
}

void hud_on_pointer_enter(Widget *w, int x, int y) {
    (void)x; (void)y;
    w->s.hud.ptr_inside = 1;
    w->s.hud.hide_at_ms = 0;
    show(w);
}
void hud_on_pointer_leave(Widget *w) {
    w->s.hud.ptr_inside = 0;
    if (now_ms() - last_btn_ms < HUD_CLICK_GRACE_MS) return;
    w->s.hud.hide_at_ms = now_ms() + HUD_HIDE_DELAY_MS;
}
void hud_on_pointer_motion(Widget *w, int x, int y) {
    (void)w; (void)x; (void)y;
}
void hud_on_pointer_button(Widget *w, uint32_t button, uint32_t state) {
    last_btn_ms = now_ms();
    if (!w->s.hud.visible || button != 0x110 /*BTN_LEFT*/) return;
    if (state == 1) {
        int b = hit(ptr_x, ptr_y, (int)w->s.hud.cur_oy);
        if (b < 0) return;
        w->s.hud.held_btn = b;
        press(w, b);
        hud_render(w, (int)w->s.hud.cur_oy);
    } else {
        if (w->s.hud.held_btn < 0) return;
        w->s.hud.held_btn = -1;
        hud_render(w, (int)w->s.hud.cur_oy);
    }
}

/* Called once per main-loop tick. Returns ms until next deadline (-1 = none).
 * Walks every HUD widget; with multi-output, each has independent
 * hover/animation state. */
int hud_check_deferred(int64_t now) {
    int timeout = -1;
    int any_visible = 0;
    for (int i = 0; i < MAX_WIDGETS; i++) {
        Widget *w = &widgets[i];
        if (w->kind != W_HUD) continue;
        if (w->s.hud.hide_at_ms) {
            int64_t left = w->s.hud.hide_at_ms - now;
            if (left <= 0) {
                w->s.hud.hide_at_ms = 0;
                if (w->s.hud.visible && !w->s.hud.ptr_inside) hide(w);
            } else if (timeout < 0 || left < timeout) timeout = (int)left;
        }
        if (w->s.hud.visible) any_visible = 1;
    }
    if (any_visible) {
        int64_t pl = (last_state_probe_ms + 5000) - now;
        if (pl <= 0) {
            hud_probe_states();
            for (int i = 0; i < MAX_WIDGETS; i++) {
                Widget *w = &widgets[i];
                if (w->kind == W_HUD && w->s.hud.visible)
                    hud_render(w, (int)w->s.hud.cur_oy);
            }
        } else if (timeout < 0 || pl < timeout) timeout = (int)pl;
    }
    return timeout;
}

void hud_create_on(Output *o) {
    if (!o || o->hud) return;
    Widget *w = widget_alloc(W_HUD);
    if (!w) { msg("twl: no widget slot for hud"); return; }
    o->hud = w;
    w->w = WIDGET_W;
    w->h = WIDGET_H;
    w->s.hud.held_btn = -1;
    w->s.hud.cur_oy = w->s.hud.target_oy = -WIDGET_H;
    widget_setup_surface(w, LAYER_OVERLAY, "twl-hud", o);
    widget_set_size(w, WIDGET_W, WIDGET_H);
    widget_set_anchor(w, LS_ANCHOR_TOP);
    widget_set_exclusive_zone(w, -1);
    widget_set_kbd_interactive(w, 0);
    w->s.hud.region_trigger = widget_make_region(0, 0, WIDGET_W, HUD_TRIG_H);
    w->s.hud.region_full    = widget_make_region(0, 0, WIDGET_W, WIDGET_H);
    widget_set_input_region(w, w->s.hud.region_trigger);
    wl_req(w->surface, SURFACE_REQ_COMMIT, NULL, 0, -1);
    /* Initial probe deferred to the first hud_on_pointer_enter — show() already
     * runs hud_probe_states on the visible→visible transition. Doing it at
     * create-time would race against signalfd setup in main() and (when it
     * was synchronous) added hundreds of ms of pre-loop stall. */
}
