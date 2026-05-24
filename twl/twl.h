/* twl — single header. All shared types + cross-module decls. */
#ifndef TWL_H
#define TWL_H

#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#include "proto.h"
#include "bake.h"
#include "config.h"

/* ============================================================ */
/* Geometry / config                                             */
/* ============================================================ */

#define MAX_OUTPUTS 8
/* MAX_WIDGETS scales with MAX_OUTPUTS: per-output bar/wall/hud/lock = 4,
 * plus 1 OSD + a couple of slots for transient menus = 4*MAX_OUTPUTS + 8. */
#define MAX_WIDGETS (4 * MAX_OUTPUTS + 8)
#define MAX_CLIENTS 16
#define MAX_TAGS    9
#define MAX_TEXT    512
#define MAX_BUTTONS 16
#define MAX_ITEMS   256
#define ITEM_MAX    160
#define MAX_OSDS    8
#define OSD_SUM_MAX 160
#define OSD_BODY_MAX 256
#define OSD_MAX_BODY_LINES 4

/* Forward decls (Output and Widget reference each other). */
typedef struct Output Output;
typedef struct Widget Widget;

/* ============================================================ */
/* Wayland I/O (wl.c)                                            */
/* ============================================================ */

extern int      wl_fd;
extern uint32_t wl_next_id;
extern uint8_t  wl_rbuf[8192];
extern int      wl_rlen;

uint32_t wl_new_id(void);
int  pad4(int x);

void wl_send(const void *buf, unsigned len, int fd);
void wl_req(uint32_t obj, uint16_t op, const uint32_t *args, int n, int fd);
void wl_req_str(uint32_t obj, uint16_t op, const uint32_t *pre, int npre,
                const char *s, const uint32_t *post, int npost);
void wl_registry_bind(uint32_t name, const char *iface, uint32_t version,
                      uint32_t new_oid);

int  wl_recv(int block);
int  wl_take_pending_fd(void);
void wl_close_pending_fds(void);

void wl_connect(void);
void wl_dispatch(void);

/* Bound globals (set by registry handling). Per-output object ids
 * (wl_output, zdwl_ipc_output_v2, zwlr_gamma_control_v1, lock_surface)
 * live on the Output struct below — these are session-wide singletons. */
extern uint32_t id_compositor, id_shm, id_seat;
extern uint32_t id_layer_shell, id_wm_base;
extern uint32_t id_pointer, id_keyboard;
extern uint32_t id_dwl_mgr;
extern uint32_t id_gamma_mgr;
extern uint32_t id_slock_mgr, id_slock;

/* Input routing state (set by pointer/keyboard events). */
extern uint32_t ptr_focus, kbd_focus;
extern int      ptr_x, ptr_y;
extern uint32_t enter_serial;

/* Key-repeat (twl.c). */
extern int      key_rep_tfd;
extern uint32_t key_rep_key;
extern int      key_rep_delay_ms, key_rep_rate_ms;

/* ============================================================ */
/* Widget abstraction (widget.c)                                 */
/* ============================================================ */

typedef enum { W_NONE = 0, W_BAR, W_HUD, W_MENU, W_OSD, W_WALL, W_LOCK } WidgetKind;

/* ============================================================ */
/* Per-output state (one per connected monitor)                  */
/* ============================================================ */

struct Output {
    int      active;                 /* slot in use */
    uint32_t registry_name;          /* wl_registry global name (for hotplug) */
    uint32_t wl_output;              /* bound wl_output object id */
    uint32_t dwl_ipc_output;         /* zdwl_ipc_output_v2; 0 if not bound */
    uint32_t gamma_ctrl;             /* zwlr_gamma_control_v1; 0 if not held */
    uint32_t gamma_size;             /* ramp size (set by gamma_size event) */
    int      gamma_failed;           /* sticky once compositor sent FAILED */
    int      last_applied_k;         /* last Kelvin written to this output */
    int      widgets_created;        /* bar/wall/hud spawned for this output */
    struct Widget *bar, *wall, *hud, *lock;
    /* dwl-ipc tag accumulator: one TAG event per tag between FRAME commits. */
    uint32_t tag_acc_occ, tag_acc_act, tag_acc_urg;
    /* Latched tag state used by bar render. */
    uint32_t tag_mask, active_mask, urgent_mask;
    int      have_tags;
    int      is_active;              /* dwl-ipc reported this output focused */
};

extern Output  outputs[MAX_OUTPUTS];
extern Output *focused_output;       /* currently focused (kbd) output; NULL = none */

Output *output_alloc(uint32_t registry_name);
Output *output_by_wl(uint32_t wl_output);
Output *output_by_ipc(uint32_t dwl_ipc);
Output *output_by_gamma(uint32_t gamma_ctrl);
Output *output_by_registry_name(uint32_t name);
void    output_destroy(Output *o);
void    output_init_widgets(Output *o);   /* spawn bar/wall/hud + ipc + gamma */
int     output_count(void);

typedef struct {
    int      active;
    uint32_t replace_id;         /* synchronous-id / dbus notification id */
    char     summary[OSD_SUM_MAX];
    char     body[OSD_BODY_MAX];
    uint32_t icon_cp;            /* nerd-font codepoint, 0 = none */
    int      progress;           /* 0..100, -1 = no bar */
    int      urgency;            /* 0=low 1=normal 2=critical */
    int      muted;              /* category=="muted" → red styling */
    int64_t  expires_at_ms;      /* 0 = sticky */
    int      h;                  /* rendered slab height; filled by osd_render */
} Osd;

typedef struct {
    uint32_t id;
    uint32_t *px;
    int      busy;
    int      off;          /* byte offset in pool */
} BufSlot;

struct Widget {
    WidgetKind kind;
    /* Output this widget is bound to (NULL for menu/osd which are output-
     * agnostic at creation time; osd then re-anchors per focused_output). */
    Output    *output;
    uint32_t   surface;
    uint32_t   layer_surface;
    int        configured;
    int        w, h;
    int        client_fd;            /* deferred-reply fd, -1 if none */

    /* SHM pool: one pool, up to 2 slots ping-pong. */
    int        pool_fd;
    int        pool_size;
    uint8_t   *shm_base;
    uint32_t   id_pool;
    int        n_slots;
    BufSlot    slots[2];

    /* Frame callback for animation (NULL when idle). */
    uint32_t   frame_cb;

    /* Auto-managed input region (destroyed+recreated by widget_set_input_region_rect).
       Widgets that pre-create regions (HUD's trigger/full) leave this 0 and swap by id. */
    uint32_t   input_region_id;

    /* When set, on_buffer_release frees the SHM pool once every slot is idle.
       Used by OSD after committing a transparent buffer following last dismissal. */
    int        want_pool_free;

    /* Widget-specific state (one big union avoids per-widget allocation). */
    union {
        struct {
            uint32_t tag_mask;        /* bit i set => tag i is occupied */
            uint32_t active_mask;     /* bit i set => tag i is active   */
            uint32_t urgent_mask;     /* bit i set => tag i is urgent   */
            char     title[MAX_TEXT];
            int      have_tags;
            uint32_t render_hash;     /* FNV-1a over displayed state; 0 = uninit */
        } bar;
        struct {
            double   cur_oy, target_oy;
            int64_t  anim_last_ms;
            int64_t  hide_at_ms;
            int      animating;
            int      visible;
            int      held_btn;
            int      ptr_inside;
            uint32_t region_trigger;
            uint32_t region_full;
            int      btn_state[MAX_BUTTONS];  /* 0=off 1=on 2=warn */
        } hud;
        struct {
            char     items[MAX_ITEMS][ITEM_MAX];
            int      n_items;
            int      filtered[MAX_ITEMS];
            int      n_filtered;
            int      sel;
            char     query[128];
            int      query_len;
            int      mods;            /* xkb-free: tracked from key events */
        } menu;
        struct {
            Osd      items[MAX_OSDS];
            uint32_t next_id;
            int      has_pixels;    /* widget currently has non-empty content */
        } osd;
        struct {
            uint32_t slock_surf_id;  /* this lock surface's object id */
        } lock;
    } s;
};

extern Widget widgets[MAX_WIDGETS];

Widget *widget_alloc(WidgetKind k);
Widget *widget_by_surface(uint32_t sid);
Widget *widget_by_ls(uint32_t lsid);
Widget *widget_by_slock_surf(uint32_t id);
Widget *widget_first(WidgetKind k);
void    widget_destroy(Widget *w);
/* Pass NULL for `o` to let the compositor pick the output (dwl ships layer-
 * shell v3 which requires a non-null output, so callers must supply one in
 * practice — but we still send the call to keep upstream-future-compat). */
void    widget_setup_surface(Widget *w, uint32_t layer, const char *ns, Output *o);
void    widget_ensure_pool(Widget *w, int n_slots);
/* Release the SHM pool (destroys buffers, destroys pool, munmaps, closes fd).
 * Safe to call after a frame.done has confirmed the compositor consumed the
 * last attached buffer — the compositor keeps its texture reference, so any
 * displayed content stays on screen until another buffer is attached. */
void    widget_free_pool(Widget *w);
BufSlot *widget_free_slot(Widget *w);
void    widget_attach(Widget *w, BufSlot *s, int request_frame);
void    widget_set_anchor(Widget *w, uint32_t anchor_bits);
void    widget_set_size(Widget *w, int width, int height);
void    widget_set_margin(Widget *w, int top, int right, int bot, int left);
void    widget_set_exclusive_zone(Widget *w, int zone);
void    widget_set_kbd_interactive(Widget *w, int on);
void    widget_set_input_region(Widget *w, uint32_t region_id);
uint32_t widget_make_region(int x, int y, int w, int h);
/* Atomic replace: destroy the prior auto-managed input region (if any),
   create+set a new one. Use this for any region that changes over time. */
void    widget_set_input_region_rect(Widget *w, int x, int y, int ww, int hh);

/* ============================================================ */
/* Rendering (render.c)                                          */
/* ============================================================ */

/* All colors are 0xAARRGGBB premultiplied at composite time. */
void clear_buf(uint32_t *px, int w, int h, uint32_t c);
void fill_rect(uint32_t *px, int sw, int sh, int x, int y, int w, int h, uint32_t c);

int  utf8_decode(const char *s, uint32_t *cp);
const Glyph *font_find(const Font *f, uint32_t cp);
int  text_width(const Font *f, const char *s);
void draw_text(uint32_t *px, int sw, int sh, int x, int y,
               const Font *f, const char *s, uint32_t fg);
void draw_glyph(uint32_t *px, int sw, int sh, int x, int y,
                const Font *f, const Glyph *g, uint32_t fg);

/* ============================================================ */
/* Status sampling (status.c)                                    */
/* ============================================================ */

typedef struct {
    int cpu_t10;       /* CPU% × 10 */
    int cpu_temp;      /* °C or -1  */
    int disk_pct;
    int mem_used_kb;   /* RAM used, kB; -1 if /proc/meminfo missing */
    int bat_pct;       /* -1 if no battery */
    int bat_charging;
    int vpn_state;     /* 0=off 1=on 2=stale */
    int wifi_level;    /* -1=off, 0..3 */
} Status;

extern Status status;

void status_init(void);
void status_tick(int tick_n);   /* called once per second */
void status_sample_all(void);

/* ============================================================ */
/* Bar (bar.c)                                                   */
/* ============================================================ */

void bar_create_on(Output *o);
void bar_render(Widget *w);
void bar_redraw_all(void);
/* Broadcast helpers: external `twlctl bar tags`/`bar title` callers don't
 * specify an output, so we apply to every connected bar. */
void bar_set_tags(uint32_t mask, uint32_t active, uint32_t urgent);
void bar_set_title(const char *s);
/* Per-output: dwl-ipc commits push directly via these. */
void bar_set_tags_on(Output *o, uint32_t mask, uint32_t active, uint32_t urgent);
void bar_set_title_on(Output *o, const char *s);

/* ============================================================ */
/* HUD button panel (hud.c)                                      */
/* ============================================================ */

void hud_create_on(Output *o);
void hud_render(Widget *w, int oy);
void hud_tick(Widget *w, int64_t now);
void hud_on_pointer_enter(Widget *w, int x, int y);
void hud_on_pointer_leave(Widget *w);
void hud_on_pointer_motion(Widget *w, int x, int y);
void hud_on_pointer_button(Widget *w, uint32_t button, uint32_t state);
int  hud_check_deferred(int64_t now);   /* returns timeout-ms or -1 */
void hud_probe_states(void);
void hud_on_sigchld(void);              /* drains waitpid + updates probe state */

/* ============================================================ */
/* Menu (dmenu replacement) (menu.c)                             */
/* ============================================================ */

Widget *menu_create(const char *title, char items[][ITEM_MAX], int n,
                    int client_fd);
Widget *menu_create_action(const char *title,
                           char items[][ITEM_MAX], char actions[][ITEM_MAX],
                           int n);
void    menu_render(Widget *w);
void    menu_on_key(Widget *w, uint32_t key, uint32_t state);
void    menu_on_click(Widget *w, int x);
void    menu_reply_and_close(Widget *w, int picked);
void    menu_cancel_all(void);

/* ============================================================ */
/* OSD / notifications (osd.c)                                   */
/* ============================================================ */

extern int dnd_on;

/* OSD widget is created on demand (and re-anchored if focus moves to a
 * different output) — no startup constructor. */
void     osd_render(Widget *w);
uint32_t osd_post(uint32_t replace_id, const char *summary, const char *body,
                  uint32_t icon_cp, int progress, int urgency, int muted,
                  int timeout_ms);
void     osd_close(uint32_t id);
void     osd_close_all(void);
int      osd_check_expiry(int64_t now);  /* returns ms-until-next or -1 */
void     osd_on_click(Widget *w, int x, int y);

/* ============================================================ */
/* Wallpaper (wall.c)                                            */
/* ============================================================ */

void wall_create_on(Output *o);
void wall_render(Widget *w);

/* ============================================================ */
/* Gamma / night mode (gamma.c)                                  */
/* ============================================================ */

typedef enum {
    GM_AUTO = 0,   /* follow schedule */
    GM_DAY,        /* force day temperature */
    GM_NIGHT,      /* force night temperature */
    GM_FLAT,       /* force flat warm (HUD override, warmer than night) */
    GM_OFF,        /* no gamma applied (passthrough) */
} GammaMode;

void     gamma_init(void);                       /* one-shot: bind for all current outputs */
void     gamma_bind_output(Output *o);           /* request gamma_control for a new output */
void     gamma_on_size(Output *o, uint32_t size);
void     gamma_on_failed(Output *o);
void     gamma_set_mode(GammaMode m);
void     gamma_tick(int tick_n);
int      gamma_is_warm(void);            /* 1 if currently warming the screen */
const char *gamma_mode_str(void);        /* one of "auto-day", "auto-night", "day", "night", "flat", "off" */

/* ============================================================ */
/* Session lock (lock.c)                                         */
/* ============================================================ */

void lock_engage(void);                  /* request session lock */
void lock_on_locked(void);               /* compositor confirmed lock */
void lock_on_finished(void);             /* lock rejected / forcibly ended */
void lock_on_surf_configure(Widget *w, uint32_t serial, int width, int height);
void lock_on_key(Widget *w, uint32_t key, uint32_t state, uint32_t mods);
void lock_on_helper_event(void);         /* helper pipe became readable */
int  lock_helper_fd(void);               /* -1 when no helper running */
int  lock_active(void);
/* Hotplug: spawn a lock surface for an output that arrives mid-lock,
 * and quietly drop one when its output goes away. */
void lock_on_output_added(Output *o);
void lock_on_output_removed(Output *o);
void lock_render_all(void);              /* re-render every lock widget */

/* ============================================================ */
/* Media controls (media.c)                                      */
/* ============================================================ */

void media_volume(const char *arg);    /* "up" | "down" | "mute" */
void media_mic(const char *arg);       /* "mute" */
void media_backlight(const char *arg); /* "up" | "down" */

/* ============================================================ */
/* D-Bus notification server (dbus.c) — optional                 */
/* ============================================================ */

int      dbus_connect(void);                              /* fd or -1 */
extern int dbus_fd;
void     dbus_dispatch(void);
void     dbus_emit_closed(uint32_t id, uint32_t reason);  /* signal NotificationClosed */

/* ============================================================ */
/* Control socket (ctl.c)                                        */
/* ============================================================ */

typedef struct {
    int  fd;
    char buf[2048];
    int  len;
} Client;

extern Client    clients[MAX_CLIENTS];
extern int       ctl_fd;
extern char      ctl_path[128];

void  ctl_open(void);
void  ctl_close(void);
void  ctl_accept(void);
void  ctl_read(Client *c);

/* ============================================================ */
/* xkb keymap (xkb.c)                                            */
/* ============================================================ */

typedef struct {
    uint32_t lo, hi;     /* level-1 and level-2 keysyms (codepoints) */
    uint8_t  alpha;      /* 1 if caps-lock should swap lo↔hi */
} XkbKey;

extern XkbKey xkb_keys[256];
extern int    xkb_loaded;
extern int    xkb_caps_on;
extern int    xkb_shift_on;

void     xkb_load(int fd, size_t size);
uint32_t xkb_xlat(uint32_t evdev, int shift);
void     xkb_on_modifiers(uint32_t depressed, uint32_t latched, uint32_t locked);
void     lock_on_caps_changed(void);  /* implemented in lock.c; called from xkb */
int      utf8_encode(uint32_t cp, char *out);
int      utf8_back(const char *s, int len);

/* ============================================================ */
/* Utilities                                                     */
/* ============================================================ */

void msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
__attribute__((noreturn)) void die(const char *fmt, ...);

int64_t now_ms(void);

/* epoll plumbing (twl.c) — ctl.c calls these on accept/close so the main loop
   doesn't have to rebuild client fd registrations every wakeup. */
void epoll_add_fd(int fd);
void epoll_del_fd(int fd);

#endif
