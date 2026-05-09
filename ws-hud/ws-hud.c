/* ws-hud — hover-revealed HUD that slides down from the bar. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcft/fcft.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <math.h>
#include <pixman-1/pixman.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/signalfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-cursor.h>

#include "config.h"
#include "xdg-shell-protocol.h"
#include "wlr-layer-shell-unstable-v1-protocol.h"

/* All knobs live in root config.h as WS_HUD_*. The #ifndef defaults below
   are only fall-backs for builds that don't include that header. */

/* colours */
#ifndef WS_HUD_BG
#define WS_HUD_BG    0xcc000000u
#endif
#ifndef WS_HUD_FG
#define WS_HUD_FG    0xff1e3a3au
#endif
#ifndef WS_HUD_ON
#define WS_HUD_ON    0xff3a7268u
#endif
#ifndef WS_HUD_HOLD
#define WS_HUD_HOLD  0xff3a7268u
#endif
#ifndef WS_HUD_ICON
#define WS_HUD_ICON  0xffffffffu
#endif
#ifndef WS_HUD_FONT
#define WS_HUD_FONT  "FiraCode Nerd Font:size=22"
#endif

/* geometry */
#ifndef WS_HUD_BTN_W
#define WS_HUD_BTN_W         60
#endif
#ifndef WS_HUD_BTN_H
#define WS_HUD_BTN_H         60
#endif
#ifndef WS_HUD_BTN_GAP
#define WS_HUD_BTN_GAP       12
#endif
#ifndef WS_HUD_BTN_BORDER_PX
#define WS_HUD_BTN_BORDER_PX 2
#endif
#ifndef WS_HUD_PAD
#define WS_HUD_PAD           10
#endif
#ifndef WS_HUD_OUTER_PX
#define WS_HUD_OUTER_PX      2
#endif
#ifndef WS_HUD_BAR_H
#define WS_HUD_BAR_H         28
#endif
#ifndef WS_HUD_BTN_OVERLAP
#define WS_HUD_BTN_OVERLAP   16
#endif
#ifndef WS_HUD_TRIG_H
#define WS_HUD_TRIG_H        5
#endif

/* timing (ms) */
#ifndef WS_HUD_HIDE_DELAY_MS
#define WS_HUD_HIDE_DELAY_MS    30
#endif
#ifndef WS_HUD_CLICK_GRACE_MS
#define WS_HUD_CLICK_GRACE_MS   100
#endif
#ifndef WS_HUD_ANIM_TAU_MS
#define WS_HUD_ANIM_TAU_MS      35.0
#endif
#ifndef WS_HUD_ANIM_FRAME_MS
#define WS_HUD_ANIM_FRAME_MS    16
#endif
#ifndef WS_HUD_ANIM_EPSILON
#define WS_HUD_ANIM_EPSILON     0.5
#endif
#ifndef WS_HUD_STARTUP_GRACE_MS
#define WS_HUD_STARTUP_GRACE_MS 800
#endif

#define BTN_W          WS_HUD_BTN_W
#define BTN_H          WS_HUD_BTN_H
#define BTN_GAP        WS_HUD_BTN_GAP
#define BTN_BORDER_PX  WS_HUD_BTN_BORDER_PX
#define PAD            WS_HUD_PAD
#define OUTER_PX       WS_HUD_OUTER_PX
#define TRIG_H         WS_HUD_TRIG_H
#define ANIM_TAU_MS    WS_HUD_ANIM_TAU_MS
#define ANIM_EPSILON   WS_HUD_ANIM_EPSILON
#define ANIM_FRAME_MS  WS_HUD_ANIM_FRAME_MS

#define WIDGET_W (BTN_COUNT * BTN_W + (BTN_COUNT - 1) * BTN_GAP + 2 * PAD)
#define BTN_OFFSET_Y (WS_HUD_BAR_H - WS_HUD_BTN_OVERLAP)
#define WIDGET_H (BTN_OFFSET_Y + BTN_H + PAD)

enum { CLICK = 0, TOGGLE = 1 };

struct button {
	int type;
	const char *a;
	const char *b;
	const char *state_cmd;  /* startup probe — sets state from exit code */
	uint32_t icon;
	int state;              /* runtime; not in config */
};

#ifndef WS_HUD_BUTTONS
#define WS_HUD_BUTTONS \
	{ CLICK,  NULL, NULL, NULL, 0xf015 }, \
	{ CLICK,  NULL, NULL, NULL, 0xf001 }, \
	{ TOGGLE, NULL, NULL, NULL, 0xf013 }, \
	{ TOGGLE, NULL, NULL, NULL, 0xf011 }
#endif

static struct button buttons[] = { WS_HUD_BUTTONS };
#define BTN_COUNT ((int)(sizeof(buttons) / sizeof(buttons[0])))

static int held_btn = -1;
static int64_t last_btn_ms;

#define MAX_MON 8

/* Each monitor owns a persistent SHM pool with two ping-pong widget buffers
   plus a single clear (transparent) buffer. The compositor toggles `busy` via
   the wl_buffer.release event; we render into whichever buffer is free. */
struct buf_slot {
	struct wl_buffer *wl;
	uint32_t *px;
	int busy;
};

struct mon {
	uint32_t name;
	struct wl_output *output;
	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *ls;
	int configured, visible;
	int64_t hide_at_ms;
	/* slide animation: cur_oy → target_oy (px). 0 = fully shown, -WIDGET_H = fully hidden */
	double cur_oy, target_oy;
	int64_t anim_last_ms;
	int animating;
	struct wl_callback *frame_cb; /* compositor-paced animation tick */
	int64_t configured_at_ms;
	/* persistent buffers */
	struct buf_slot slots[2];
	struct wl_buffer *clear;
	uint8_t *shm_base;
	size_t shm_size;
	int last_oy_drawn;     /* last oy painted into a buffer, for change-detect */
	uint64_t last_state_sig;
};

static int64_t
now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static struct mon mons[MAX_MON];
static int n_mons;
static int startup_done;

static struct wl_compositor       *compositor;
static struct wl_shm              *shm;
static struct wl_seat             *seat;
static struct wl_pointer          *pointer;
static struct zwlr_layer_shell_v1 *layer_shell;
static struct wl_region           *region_trigger, *region_full;
static struct fcft_font           *font;

static struct mon *cur_mon;
static int cur_x, cur_y;

static struct wl_cursor_theme *cursor_theme;
static struct wl_cursor       *cursor_arrow, *cursor_hand;
static struct wl_surface      *cursor_surface;
static uint32_t                enter_serial;
static int                     cursor_kind = -1; /* 0=arrow, 1=hand */

static void noop() {}

/* ---------- helpers ---------- */

static void
run_cmd(const char *cmd)
{
	if (!cmd) return;
	pid_t pid = fork();
	if (pid == 0) {
		setsid();
		execl("/bin/sh", "sh", "-c", cmd, (char*)NULL);
		_exit(127);
	}
}

/* Run cmd to completion and return its exit code (or -1 on fork/wait error).
   Works regardless of whether SIGCHLD has been set to SIG_IGN: we restore
   SIG_DFL for the duration of the wait, then drain any zombies that other
   async run_cmd children left behind before putting SIG_IGN back. */
static int
run_cmd_sync(const char *cmd)
{
	if (!cmd) return -1;
	struct sigaction old, def = { .sa_handler = SIG_DFL };
	sigemptyset(&def.sa_mask);
	sigaction(SIGCHLD, &def, &old);

	pid_t pid = fork();
	int rc = -1;
	if (pid == 0) {
		execl("/bin/sh", "sh", "-c", cmd, (char*)NULL);
		_exit(127);
	}
	if (pid > 0) {
		int st;
		while (waitpid(pid, &st, 0) < 0)
			if (errno != EINTR) break; else continue;
		rc = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
	}
	while (waitpid(-1, NULL, WNOHANG) > 0)
		;
	sigaction(SIGCHLD, &old, NULL);
	return rc;
}

static int64_t last_state_probe_ms;

/* Returns 1 if any button's state changed (caller can repaint). */
static int
probe_button_states(void)
{
	int changed = 0;
	for (int i = 0; i < BTN_COUNT; i++) {
		if (!buttons[i].state_cmd) continue;
		int s = (run_cmd_sync(buttons[i].state_cmd) == 0);
		if (s != buttons[i].state) { buttons[i].state = s; changed = 1; }
	}
	last_state_probe_ms = now_ms();
	return changed;
}

static void
fill_rect(uint32_t *px, int stride, int x, int y, int w, int h, uint32_t c)
{
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
			px[(y + j) * stride + (x + i)] = c;
}

/* clipped rect fill: silently drops anything outside [0,W)×[0,H) */
static void
cfill(uint32_t *px, int W, int H, int x, int y, int w, int h, uint32_t c)
{
	int x0 = x < 0 ? 0 : x;
	int y0 = y < 0 ? 0 : y;
	int x1 = x + w > W ? W : x + w;
	int y1 = y + h > H ? H : y + h;
	if (x0 >= x1 || y0 >= y1) return;
	fill_rect(px, W, x0, y0, x1 - x0, y1 - y0, c);
}

static int
shm_alloc(int size)
{
	int fd = memfd_create("ws-hud", MFD_CLOEXEC);
	if (fd < 0 || ftruncate(fd, size) < 0) { perror("shm"); exit(1); }
	return fd;
}

static int
btn_x(int i) { return PAD + i * (BTN_W + BTN_GAP); }

static void
argb_to_pixman(uint32_t argb, pixman_color_t *pc)
{
	uint8_t a = (argb >> 24) & 0xff;
	uint8_t r = (argb >> 16) & 0xff;
	uint8_t g = (argb >>  8) & 0xff;
	uint8_t b = (argb      ) & 0xff;
	pc->alpha = a * 0x101;
	pc->red   = (r * a / 255) * 0x101;
	pc->green = (g * a / 255) * 0x101;
	pc->blue  = (b * a / 255) * 0x101;
}

static void
draw_icon(pixman_image_t *dst, int bx, int by, uint32_t cp, uint32_t color)
{
	if (!font || !cp) return;
	const struct fcft_glyph *g =
		fcft_rasterize_char_utf32(font, cp, FCFT_SUBPIXEL_DEFAULT);
	if (!g) return;
	int dx = bx + (BTN_W - g->width) / 2;
	int dy = by + (BTN_H - g->height) / 2;
	if (g->is_color_glyph) {
		pixman_image_composite32(PIXMAN_OP_OVER, g->pix, NULL, dst,
			0, 0, 0, 0, dx, dy, g->width, g->height);
	} else {
		pixman_color_t pc;
		argb_to_pixman(color, &pc);
		pixman_image_t *fg = pixman_image_create_solid_fill(&pc);
		pixman_image_composite32(PIXMAN_OP_OVER, fg, g->pix, dst,
			0, 0, 0, 0, dx, dy, g->width, g->height);
		pixman_image_unref(fg);
	}
}

/* ---------- buffers ---------- */

static void
slot_release(void *data, struct wl_buffer *buf)
{
	struct buf_slot *s = data;
	s->busy = 0;
}
static const struct wl_buffer_listener slot_listener = { .release = slot_release };

/* Allocate one shm region per monitor: clear-buffer + 2 widget buffers, mapped
   for the lifetime of the process. After this point we never call mmap, memfd,
   ftruncate, wl_buffer_destroy, or wl_shm_pool_create_pool again. */
static void
mon_alloc_buffers(struct mon *m)
{
	int stride = WIDGET_W * 4;
	size_t one = (size_t)stride * WIDGET_H;
	size_t total = one * 3;
	int fd = shm_alloc(total);
	uint8_t *base = mmap(NULL, total, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (base == MAP_FAILED) { perror("mmap"); exit(1); }
	struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, total);
	/* clear: zeros from memfd, never modified, no listener (keep forever) */
	m->clear = wl_shm_pool_create_buffer(pool, 0, WIDGET_W, WIDGET_H, stride,
		WL_SHM_FORMAT_ARGB8888);
	for (int i = 0; i < 2; i++) {
		size_t off = one * (i + 1);
		m->slots[i].px = (uint32_t *)(base + off);
		m->slots[i].wl = wl_shm_pool_create_buffer(pool, off, WIDGET_W,
			WIDGET_H, stride, WL_SHM_FORMAT_ARGB8888);
		m->slots[i].busy = 0;
		wl_buffer_add_listener(m->slots[i].wl, &slot_listener, &m->slots[i]);
	}
	wl_shm_pool_destroy(pool);
	close(fd);
	m->shm_base = base;
	m->shm_size = total;
	m->last_oy_drawn = INT32_MIN;
	m->last_state_sig = 0;
}

/* render the widget at vertical offset `oy` (in surface-local pixels).
   oy = 0 → fully shown. oy = -WIDGET_H → fully scrolled above the surface. */
static void
render_widget(uint32_t *px, int oy)
{
	int w = WIDGET_W, h = WIDGET_H;
	int stride = w, size = w * h * 4;

	memset(px, 0, size);

	/* body anchored at the bar's bottom edge — nothing renders above it.
	   The bottom edge tracks oy so the body collapses into the bar as the
	   widget closes (rather than its borders sliding through the bar zone). */
	int body_y      = WS_HUD_BAR_H;
	int body_bottom = WIDGET_H + oy;
	int body_h      = body_bottom - body_y;
	if (body_h > 0) {
		cfill(px, w, h, 0,            body_y,                  w,        body_h,   WS_HUD_BG);
		cfill(px, w, h, 0,            body_y,                  OUTER_PX, body_h,   WS_HUD_FG);
		cfill(px, w, h, w - OUTER_PX, body_y,                  OUTER_PX, body_h,   WS_HUD_FG);
		cfill(px, w, h, 0,            body_bottom - OUTER_PX,  w,        OUTER_PX, WS_HUD_FG);
	}

	pixman_image_t *dst = pixman_image_create_bits_no_clear(
		PIXMAN_a8r8g8b8, w, h, (uint32_t *)px, w * 4);

	int by = BTN_OFFSET_Y + oy;
	int t  = BTN_BORDER_PX;
	for (int i = 0; i < BTN_COUNT; i++) {
		int bx = btn_x(i);
		if (held_btn == i)
			cfill(px, w, h, bx, by, BTN_W, BTN_H, WS_HUD_HOLD);
		else if (buttons[i].type == TOGGLE && buttons[i].state)
			cfill(px, w, h, bx, by, BTN_W, BTN_H, WS_HUD_ON);
		cfill(px, w, h, bx,             by,             BTN_W, t,     WS_HUD_FG);
		cfill(px, w, h, bx,             by + BTN_H - t, BTN_W, t,     WS_HUD_FG);
		cfill(px, w, h, bx,             by,             t,     BTN_H, WS_HUD_FG);
		cfill(px, w, h, bx + BTN_W - t, by,             t,     BTN_H, WS_HUD_FG);
		draw_icon(dst, bx, by, buttons[i].icon, WS_HUD_ICON);
	}

	pixman_image_unref(dst);
	(void)stride;
}

/* ---------- cursor ---------- */

static void
load_cursors(void)
{
	const char *theme = getenv("XCURSOR_THEME");
	int size = 24;
	const char *sz = getenv("XCURSOR_SIZE");
	if (sz) { int v = atoi(sz); if (v > 0) size = v; }
	cursor_theme = wl_cursor_theme_load(theme, size, shm);
	if (!cursor_theme) return;
	cursor_arrow = wl_cursor_theme_get_cursor(cursor_theme, "left_ptr");
	if (!cursor_arrow) cursor_arrow = wl_cursor_theme_get_cursor(cursor_theme, "default");
	cursor_hand  = wl_cursor_theme_get_cursor(cursor_theme, "pointer");
	if (!cursor_hand) cursor_hand = wl_cursor_theme_get_cursor(cursor_theme, "hand2");
	if (!cursor_hand) cursor_hand = wl_cursor_theme_get_cursor(cursor_theme, "hand1");
	cursor_surface = wl_compositor_create_surface(compositor);
}

static void
set_cursor(int kind)
{
	if (!pointer || !cursor_surface || kind == cursor_kind) return;
	cursor_kind = kind;
	struct wl_cursor *c = (kind == 1) ? cursor_hand : cursor_arrow;
	if (!c || c->image_count == 0) {
		wl_pointer_set_cursor(pointer, enter_serial, NULL, 0, 0);
		return;
	}
	struct wl_cursor_image *img = c->images[0];
	struct wl_buffer *b = wl_cursor_image_get_buffer(img);
	if (!b) return;
	wl_pointer_set_cursor(pointer, enter_serial, cursor_surface,
	                      img->hotspot_x, img->hotspot_y);
	wl_surface_attach(cursor_surface, b, 0, 0);
	wl_surface_damage_buffer(cursor_surface, 0, 0, img->width, img->height);
	wl_surface_commit(cursor_surface);
}

/* ---------- hit testing ---------- */

static int
hit(int x, int y)
{
	if (y < BTN_OFFSET_Y || y >= BTN_OFFSET_Y + BTN_H) return -1;
	for (int i = 0; i < BTN_COUNT; i++) {
		int bx = btn_x(i);
		if (x >= bx && x < bx + BTN_W) return i;
	}
	return -1;
}

/* ---------- rendering / animation ---------- */

static void tick(struct mon *m, int64_t now);

/* While animating we let the compositor pace us via wl_surface.frame: the
   callback fires when it's ready for our next frame, which (a) avoids the
   16ms-poll/buffer-release race that caused stutter, and (b) guarantees the
   other ping-pong slot is free by the time we render again. */
static void
frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
	struct mon *m = data;
	wl_callback_destroy(cb);
	if (m->frame_cb == cb) m->frame_cb = NULL;
	if (m->animating) tick(m, now_ms());
}
static const struct wl_callback_listener frame_listener = { .done = frame_done };

static void
attach_buf(struct mon *m, struct wl_buffer *b, int request_frame)
{
	wl_surface_attach(m->surface, b, 0, 0);
	wl_surface_damage_buffer(m->surface, 0, 0, WIDGET_W, WIDGET_H);
	if (request_frame && !m->frame_cb) {
		m->frame_cb = wl_surface_frame(m->surface);
		wl_callback_add_listener(m->frame_cb, &frame_listener, m);
	}
	wl_surface_commit(m->surface);
}

/* Pack render-relevant state into one word so we can skip identical re-renders.
   Includes oy (the per-frame variable), held button, and toggle states. */
static uint64_t
state_sig(int oy)
{
	uint64_t s = (uint32_t)(oy & 0xffffff);
	s |= ((uint64_t)(held_btn & 0xff)) << 24;
	for (int i = 0; i < BTN_COUNT && i < 32; i++)
		if (buttons[i].state) s |= ((uint64_t)1) << (32 + i);
	return s;
}

static void
attach_widget(struct mon *m, int oy)
{
	uint64_t sig = state_sig(oy);
	int slot = -1;
	for (int i = 0; i < 2; i++) if (!m->slots[i].busy) { slot = i; break; }
	if (slot < 0) return; /* both in flight; tick will retry */

	if (sig != m->last_state_sig || m->last_oy_drawn != oy) {
		render_widget(m->slots[slot].px, oy);
		m->last_state_sig = sig;
		m->last_oy_drawn  = oy;
	}
	m->slots[slot].busy = 1;
	attach_buf(m, m->slots[slot].wl, m->animating);
}

static void
init_mon_hidden(struct mon *m)
{
	m->visible = 0;
	m->animating = 0;
	m->cur_oy = m->target_oy = -WIDGET_H;
	wl_surface_set_input_region(m->surface, region_trigger);
	attach_buf(m, m->clear, 0);
}

static void
show(struct mon *m)
{
	if (m->target_oy == 0 && !m->animating && m->visible) return;
	int was_hidden = !m->visible;
	/* Re-probe state_cmd-bearing toggles whenever the HUD reveals after a
	   quiet period. Catches state changes the user made through other means
	   (e.g. picking a different relay in mullvad-menu, then hovering back). */
	if (was_hidden && now_ms() - last_state_probe_ms > 1500)
		probe_button_states();
	m->visible    = 1;
	m->target_oy  = 0;
	m->animating  = 1;
	m->anim_last_ms = now_ms();
	if (was_hidden) {
		wl_surface_set_input_region(m->surface, region_full);
		attach_widget(m, (int)m->cur_oy);
	}
}

static void
hide(struct mon *m)
{
	if (m->target_oy == -WIDGET_H && !m->animating) return;
	int was_idle = !m->animating;
	m->target_oy   = -WIDGET_H;
	m->animating   = 1;
	m->anim_last_ms = now_ms();
	if (was_idle && !m->frame_cb) attach_widget(m, (int)m->cur_oy);
}

static void
tick(struct mon *m, int64_t now)
{
	if (!m->animating) return;
	double dt = (double)(now - m->anim_last_ms);
	m->anim_last_ms = now;
	double k = 1.0 - exp(-dt / ANIM_TAU_MS);
	m->cur_oy += (m->target_oy - m->cur_oy) * k;

	if (fabs(m->cur_oy - m->target_oy) < ANIM_EPSILON) {
		m->cur_oy   = m->target_oy;
		m->animating = 0;
		if (m->target_oy == -WIDGET_H) {
			m->visible = 0;
			wl_surface_set_input_region(m->surface, region_trigger);
			attach_buf(m, m->clear, 0);
			return;
		}
	}
	attach_widget(m, (int)m->cur_oy);
}

static void
repaint_visible(void)
{
	for (int i = 0; i < n_mons; i++)
		if (mons[i].visible)
			attach_widget(&mons[i], (int)mons[i].cur_oy);
}

static void
press(int idx)
{
	struct button *b = &buttons[idx];
	if (b->type == CLICK) { run_cmd(b->a); return; }
	run_cmd(b->state ? b->b : b->a);
	b->state = !b->state;
}

/* ---------- layer surface ---------- */

static void
ls_configure(void *data, struct zwlr_layer_surface_v1 *ls,
             uint32_t serial, uint32_t w, uint32_t h)
{
	struct mon *m = data;
	zwlr_layer_surface_v1_ack_configure(ls, serial);
	init_mon_hidden(m);
	m->configured = 1;
	m->configured_at_ms = now_ms();
}
static void ls_closed(void *d, struct zwlr_layer_surface_v1 *ls) {}
static const struct zwlr_layer_surface_v1_listener ls_listener = {
	.configure = ls_configure, .closed = ls_closed,
};

static void
setup_mon(struct mon *m)
{
	mon_alloc_buffers(m);
	m->surface = wl_compositor_create_surface(compositor);
	wl_surface_set_user_data(m->surface, m);
	m->ls = zwlr_layer_shell_v1_get_layer_surface(layer_shell, m->surface,
		m->output, ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "ws-hud");
	zwlr_layer_surface_v1_add_listener(m->ls, &ls_listener, m);
	zwlr_layer_surface_v1_set_size(m->ls, WIDGET_W, WIDGET_H);
	zwlr_layer_surface_v1_set_anchor(m->ls, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP);
	zwlr_layer_surface_v1_set_exclusive_zone(m->ls, -1);
	zwlr_layer_surface_v1_set_keyboard_interactivity(m->ls, 0);
	wl_surface_commit(m->surface);
}

/* ---------- pointer ---------- */

static void
ptr_enter(void *d, struct wl_pointer *p, uint32_t serial,
          struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
	if (!surface) return;
	struct mon *m = wl_surface_get_user_data(surface);
	if (!m) return;
	cur_mon = m;
	cur_x = wl_fixed_to_int(x);
	cur_y = wl_fixed_to_int(y);
	enter_serial = serial;
	cursor_kind = -1; /* force reapply on this enter's serial */
	set_cursor((m->visible && hit(cur_x, cur_y) >= 0) ? 1 : 0);
	m->hide_at_ms = 0;
	/* Suppress the synthetic enter the compositor fires when our surface
	   maps under an already-stationary pointer. */
	if (m->configured_at_ms &&
	    now_ms() - m->configured_at_ms < WS_HUD_STARTUP_GRACE_MS)
		return;
	show(m);
}
static void
ptr_leave(void *d, struct wl_pointer *p, uint32_t serial, struct wl_surface *surface)
{
	if (!surface) return;
	struct mon *m = wl_surface_get_user_data(surface);
	if (!m) return;
	if (cur_mon == m) cur_mon = NULL;
	if (now_ms() - last_btn_ms < WS_HUD_CLICK_GRACE_MS) return;
	m->hide_at_ms = now_ms() + WS_HUD_HIDE_DELAY_MS;
}
static void
ptr_motion(void *d, struct wl_pointer *p, uint32_t time,
           wl_fixed_t x, wl_fixed_t y)
{
	cur_x = wl_fixed_to_int(x);
	cur_y = wl_fixed_to_int(y);
	set_cursor((cur_mon && cur_mon->visible && hit(cur_x, cur_y) >= 0) ? 1 : 0);
}
static void
ptr_button(void *d, struct wl_pointer *p, uint32_t serial, uint32_t time,
           uint32_t button, uint32_t state)
{
	last_btn_ms = now_ms();
	if (!cur_mon || !cur_mon->visible) return;
	if (button != BTN_LEFT) return;

	if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
		int b = hit(cur_x, cur_y);
		if (b < 0) return;
		held_btn = b;
		press(b);
		repaint_visible();
	} else {
		if (held_btn < 0) return;
		held_btn = -1;
		repaint_visible();
	}
}

static struct wl_pointer_listener ptr_listener;

static void
seat_caps(void *d, struct wl_seat *s, uint32_t caps)
{
	if ((caps & WL_SEAT_CAPABILITY_POINTER) && !pointer) {
		pointer = wl_seat_get_pointer(s);
		ptr_listener.enter  = ptr_enter;
		ptr_listener.leave  = ptr_leave;
		ptr_listener.motion = ptr_motion;
		ptr_listener.button = ptr_button;
		ptr_listener.axis   = (void*)noop;
		wl_pointer_add_listener(pointer, &ptr_listener, NULL);
	}
}
static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_caps, .name = (void*)noop,
};

static void
reg_global(void *d, struct wl_registry *r, uint32_t name,
           const char *iface, uint32_t ver)
{
	if (!strcmp(iface, wl_compositor_interface.name))
		compositor = wl_registry_bind(r, name, &wl_compositor_interface, 4);
	else if (!strcmp(iface, wl_shm_interface.name))
		shm = wl_registry_bind(r, name, &wl_shm_interface, 1);
	else if (!strcmp(iface, zwlr_layer_shell_v1_interface.name))
		layer_shell = wl_registry_bind(r, name, &zwlr_layer_shell_v1_interface, 1);
	else if (!strcmp(iface, wl_seat_interface.name)) {
		seat = wl_registry_bind(r, name, &wl_seat_interface, 1);
		wl_seat_add_listener(seat, &seat_listener, NULL);
	} else if (!strcmp(iface, wl_output_interface.name)) {
		if (n_mons >= MAX_MON) return;
		struct mon *m = &mons[n_mons++];
		m->name   = name;
		m->output = wl_registry_bind(r, name, &wl_output_interface, 3);
		if (startup_done) /* hot-plug; initial outputs are set up by main() */
			setup_mon(m);
	}
}

static void
mon_destroy(struct mon *m)
{
	if (m->frame_cb) { wl_callback_destroy(m->frame_cb); m->frame_cb = NULL; }
	if (m->ls)      { zwlr_layer_surface_v1_destroy(m->ls); m->ls = NULL; }
	if (m->surface) { wl_surface_destroy(m->surface); m->surface = NULL; }
	if (m->clear)   { wl_buffer_destroy(m->clear); m->clear = NULL; }
	for (int i = 0; i < 2; i++) {
		if (m->slots[i].wl) {
			wl_buffer_destroy(m->slots[i].wl);
			m->slots[i].wl = NULL;
		}
	}
	if (m->shm_base) { munmap(m->shm_base, m->shm_size); m->shm_base = NULL; }
	if (m->output)  { wl_output_destroy(m->output); m->output = NULL; }
	if (cur_mon == m) cur_mon = NULL;
}

static void
reg_global_remove(void *d, struct wl_registry *r, uint32_t name)
{
	for (int i = 0; i < n_mons; i++) {
		if (mons[i].name == name) {
			mon_destroy(&mons[i]);
			mons[i] = mons[--n_mons];
			memset(&mons[n_mons], 0, sizeof(mons[n_mons]));
			return;
		}
	}
}

static const struct wl_registry_listener reg_listener = {
	.global = reg_global, .global_remove = reg_global_remove,
};

int
main(void)
{
	probe_button_states();
	signal(SIGCHLD, SIG_IGN);

	if (!fcft_init(FCFT_LOG_COLORIZE_NEVER, false, FCFT_LOG_CLASS_ERROR))
		fprintf(stderr, "fcft_init failed\n");
	const char *names[] = { WS_HUD_FONT };
	font = fcft_from_name(1, names, NULL);
	if (!font)
		fprintf(stderr, "warning: failed to load font %s — icons disabled\n",
			WS_HUD_FONT);

	struct wl_display *dpy = wl_display_connect(NULL);
	if (!dpy) { fprintf(stderr, "no wayland display\n"); return 1; }

	struct wl_registry *reg = wl_display_get_registry(dpy);
	wl_registry_add_listener(reg, &reg_listener, NULL);
	wl_display_roundtrip(dpy);
	wl_display_roundtrip(dpy);

	if (!compositor || !shm || !layer_shell) {
		fprintf(stderr, "missing wayland globals (need wlr-layer-shell)\n");
		return 1;
	}
	if (!n_mons) { fprintf(stderr, "no outputs\n"); return 1; }

	load_cursors();

	region_trigger = wl_compositor_create_region(compositor);
	wl_region_add(region_trigger, 0, 0, WIDGET_W, TRIG_H);
	region_full = wl_compositor_create_region(compositor);
	wl_region_add(region_full, 0, 0, WIDGET_W, WIDGET_H);

	for (int i = 0; i < n_mons; i++)
		setup_mon(&mons[i]);
	startup_done = 1;

	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);
	sigprocmask(SIG_BLOCK, &mask, NULL);
	int sigfd = signalfd(-1, &mask, SFD_CLOEXEC);
	if (sigfd < 0) { perror("signalfd"); return 1; }

	struct pollfd pfd[2] = {
		{ wl_display_get_fd(dpy), POLLIN, 0 },
		{ sigfd,                  POLLIN, 0 },
	};

	for (;;) {
		while (wl_display_prepare_read(dpy) != 0)
			wl_display_dispatch_pending(dpy);
		wl_display_flush(dpy);

		int timeout = -1;
		int64_t now = now_ms();
		for (int i = 0; i < n_mons; i++) {
			if (mons[i].hide_at_ms) {
				int64_t left = mons[i].hide_at_ms - now;
				if (left < 0) left = 0;
				if (timeout < 0 || left < timeout) timeout = (int)left;
			}
		}

		if (poll(pfd, 2, timeout) < 0) { wl_display_cancel_read(dpy); break; }
		if (pfd[1].revents & POLLIN)   { wl_display_cancel_read(dpy); break; }
		if (pfd[0].revents & POLLIN) {
			if (wl_display_read_events(dpy) < 0) break;
		} else {
			wl_display_cancel_read(dpy);
		}
		if (wl_display_dispatch_pending(dpy) < 0) break;

		now = now_ms();
		for (int i = 0; i < n_mons; i++) {
			if (mons[i].hide_at_ms && now >= mons[i].hide_at_ms) {
				mons[i].hide_at_ms = 0;
				if (mons[i].visible) hide(&mons[i]);
			}
		}
	}

	close(sigfd);
	if (font) fcft_destroy(font);
	fcft_fini();
	wl_display_disconnect(dpy);
	return 0;
}
