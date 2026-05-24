/* twl — main entry + event loop.
 *   epoll on { wl_fd, ctl_fd, status_tfd, signalfd, client_fds, }
 *   1Hz timerfd drives status sample + bar redraw + HUD deferred actions. */

#include "twl.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

int64_t now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ============================================================ */
/* Input event handlers (called from wl.c dispatch)              */
/* ============================================================ */

/* Key-repeat state. Wayland never auto-repeats; the client emulates it from a
 * timerfd, optionally calibrated by wl_keyboard.repeat_info. Holds the most
 * recently pressed key and re-fires menu_on_key on each tick. */
int      key_rep_tfd      = -1;
uint32_t key_rep_key      = 0;
int      key_rep_delay_ms = 400;
int      key_rep_rate_ms  = 35;

static void key_rep_arm(uint32_t key) {
    key_rep_key = key;
    if (key_rep_tfd < 0) return;
    struct itimerspec ts = {
        .it_value    = { .tv_sec = key_rep_delay_ms / 1000,
                         .tv_nsec = (key_rep_delay_ms % 1000) * 1000000L },
        .it_interval = { .tv_sec = key_rep_rate_ms / 1000,
                         .tv_nsec = (key_rep_rate_ms % 1000) * 1000000L },
    };
    timerfd_settime(key_rep_tfd, 0, &ts, NULL);
}
static void key_rep_cancel(void) {
    key_rep_key = 0;
    if (key_rep_tfd < 0) return;
    struct itimerspec off = {0};
    timerfd_settime(key_rep_tfd, 0, &off, NULL);
}

void on_pointer_event(uint16_t op, uint8_t *body, uint32_t bodylen) {
    (void)bodylen;
    switch (op) {
    case 0: {  /* enter: serial, surface, x, y */
        uint32_t serial = *(uint32_t *)body;
        ptr_focus = *(uint32_t *)(body + 4);
        int32_t fx = *(int32_t *)(body + 8), fy = *(int32_t *)(body + 12);
        ptr_x = fx >> 8; ptr_y = fy >> 8;
        enter_serial = serial;
        Widget *w = widget_by_surface(ptr_focus);
        if (w && w->kind == W_HUD) hud_on_pointer_enter(w, ptr_x, ptr_y);
        break;
    }
    case 1: {  /* leave */
        Widget *w = widget_by_surface(ptr_focus);
        if (w && w->kind == W_HUD) hud_on_pointer_leave(w);
        ptr_focus = 0;
        break;
    }
    case 2: {  /* motion */
        int32_t fx = *(int32_t *)(body + 4), fy = *(int32_t *)(body + 8);
        ptr_x = fx >> 8; ptr_y = fy >> 8;
        Widget *w = widget_by_surface(ptr_focus);
        if (w && w->kind == W_HUD) hud_on_pointer_motion(w, ptr_x, ptr_y);
        break;
    }
    case 3: {  /* button */
        uint32_t button = *(uint32_t *)(body + 8);
        uint32_t state  = *(uint32_t *)(body + 12);
        Widget *w = widget_by_surface(ptr_focus);
        if (!w) return;
        if (w->kind == W_HUD) hud_on_pointer_button(w, button, state);
        else if (w->kind == W_MENU && state == 1 && button == 0x110) {
            menu_on_click(w, ptr_x);
        }
        else if (w->kind == W_OSD && state == 1 && button == 0x110) {
            osd_on_click(w, ptr_x, ptr_y);
        }
        break;
    }
    default: break;
    }
}

void on_keyboard_event(uint16_t op, uint8_t *body, uint32_t bodylen) {
    (void)bodylen;
    switch (op) {
    case 0: {  /* keymap: format(4) fd(via cmsg) size(4) */
        uint32_t format = *(uint32_t *)body;
        uint32_t size   = *(uint32_t *)(body + 4);
        int fd = wl_take_pending_fd();
        if (fd >= 0) {
            if (format == 1) xkb_load(fd, size);   /* xkb_v1 */
            close(fd);
        }
        break;
    }
    case 1: kbd_focus = *(uint32_t *)(body + 4); break;
    case 2: kbd_focus = 0; key_rep_cancel(); break;
    case 3: {
        uint32_t key   = *(uint32_t *)(body + 8);
        uint32_t state = *(uint32_t *)(body + 12);
        Widget *w = widget_by_surface(kbd_focus);
        if (!w) return;
        if (w->kind == W_MENU) {
            menu_on_key(w, key, state);
            if (state == 1) key_rep_arm(key);
            else if (key == key_rep_key) key_rep_cancel();
        } else if (w->kind == W_LOCK) {
            lock_on_key(w, key, state, 0);
            if (state == 1) key_rep_arm(key);
            else if (key == key_rep_key) key_rep_cancel();
        }
        break;
    }
    case 4: {  /* modifiers: serial(4) dep(4) lat(4) lock(4) group(4) */
        uint32_t dep  = *(uint32_t *)(body + 4);
        uint32_t lat  = *(uint32_t *)(body + 8);
        uint32_t lck  = *(uint32_t *)(body + 12);
        xkb_on_modifiers(dep, lat, lck);
        break;
    }
    case 5: {  /* repeat_info(rate keys/s, delay ms) */
        int32_t rate  = *(int32_t *)body;
        int32_t delay = *(int32_t *)(body + 4);
        if (rate > 0)  key_rep_rate_ms  = 1000 / rate;
        if (delay > 0) key_rep_delay_ms = delay;
        break;
    }
    default: break;
    }
}

void on_ls_event(Widget *w, uint16_t op, uint8_t *body, uint32_t bodylen) {
    (void)bodylen;
    if (op == LS_EV_CONFIGURE) {
        uint32_t serial = *(uint32_t *)body;
        uint32_t nw = *(uint32_t *)(body + 4);
        uint32_t nh = *(uint32_t *)(body + 8);
        if (nw) w->w = nw;
        if (nh) w->h = nh;
        wl_req(w->layer_surface, LS_REQ_ACK_CONFIGURE, &serial, 1, -1);
        w->configured = 1;
        if (w->kind == W_BAR)  bar_render(w);
        if (w->kind == W_WALL) wall_render(w);
        if (w->kind == W_MENU) menu_render(w);
        if (w->kind == W_OSD)  osd_render(w);
        /* OSD: don't render on configure — wait for first post. Avoids
         * the ~3 MB pool until something actually needs to show. */
        if (w->kind == W_HUD) {
            /* initial hidden state: transparent buffer + trigger region. Drop
             * the ~190 KB pool right after the compositor consumes the first
             * frame — hud_render reallocates on hover. */
            BufSlot *s = widget_free_slot(w);
            if (!s) { widget_ensure_pool(w, 2); s = widget_free_slot(w); }
            if (s) {
                memset(s->px, 0, w->w * w->h * 4);
                widget_attach(w, s, 1);
                w->want_pool_free = 1;
            }
        }
        return;
    }
    if (op == LS_EV_CLOSED) {
        /* Multi-output: an output being torn off triggers CLOSED on its
         * bar/wall/hud surfaces. Just destroy the widget; output_destroy()
         * will run on the matching global_remove. */
        if (w->kind == W_MENU) menu_reply_and_close(w, -1);
        else                   widget_destroy(w);
        return;
    }
}

/* ============================================================ */
/* Main                                                          */
/* ============================================================ */

static int status_tfd = -1;
static int sig_fd = -1;
static int ep_fd  = -1;
static unsigned tick_n = 0;

void epoll_add_fd(int fd) {
    if (ep_fd < 0 || fd < 0) return;
    struct epoll_event ev = { .events = EPOLLIN, .data.fd = fd };
    epoll_ctl(ep_fd, EPOLL_CTL_ADD, fd, &ev);
}
void epoll_del_fd(int fd) {
    if (ep_fd < 0 || fd < 0) return;
    epoll_ctl(ep_fd, EPOLL_CTL_DEL, fd, NULL);
}

static void on_signal(int s) {
    (void)s;
    ctl_close();
    _exit(0);
}

static int arm_minute_timer(void) {
    int fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (fd < 0) return -1;
    struct itimerspec ts = {
        .it_value    = { .tv_sec = 1 },
        .it_interval = { .tv_sec = 1 },
    };
    timerfd_settime(fd, 0, &ts, NULL);
    return fd;
}

int main(void) {
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, SIG_IGN);
    /* SIGCHLD is delivered via signalfd (see below) so we can both reap
     * fire-and-forget run_cmd children and match exit codes from async HUD
     * probes (hud_on_sigchld). Block it here, before any fork(), so it can
     * never be lost by racing with signalfd creation. */
    sigset_t cmask; sigemptyset(&cmask);
    sigaddset(&cmask, SIGUSR1);
    sigaddset(&cmask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &cmask, NULL);

    wl_connect();
    if (!id_compositor)  die("compositor missing");
    if (!id_shm)         die("wl_shm missing");
    if (!id_layer_shell) die("layer-shell missing");

    /* Spawn per-output widgets + bind dwl-ipc + gamma_control for every
     * output that arrived during the startup sync. Hotplug after this point
     * runs the same init from wl.c's registry handler. */
    for (int i = 0; i < MAX_OUTPUTS; i++)
        if (outputs[i].active) output_init_widgets(&outputs[i]);

    status_init();
    status_sample_all();
    ctl_open();
    int dbus = dbus_connect();  /* -1 if session bus not available */

    status_tfd = arm_minute_timer();
    if (status_tfd < 0) die("timerfd");
    key_rep_tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);

    sig_fd = signalfd(-1, &cmask, SFD_CLOEXEC | SFD_NONBLOCK);

    ep_fd = epoll_create1(EPOLL_CLOEXEC);
    epoll_add_fd(wl_fd);
    epoll_add_fd(ctl_fd);
    epoll_add_fd(status_tfd);
    epoll_add_fd(key_rep_tfd);
    epoll_add_fd(sig_fd);
    if (dbus >= 0) epoll_add_fd(dbus);

    msg("twl: running");
    for (;;) {
        int64_t now = now_ms();
        int timeout = hud_check_deferred(now);
        int otimeout = osd_check_expiry(now);
        if (otimeout >= 0 && (timeout < 0 || otimeout < timeout)) timeout = otimeout;

        struct epoll_event evs[16];
        int n = epoll_wait(ep_fd, evs, 16, timeout);
        if (n < 0) {
            if (errno == EINTR) continue;
            die("epoll_wait: %s", strerror(errno));
        }
        for (int i = 0; i < n; i++) {
            int fd = evs[i].data.fd;
            if (fd == wl_fd) {
                while (wl_recv(0) == 0 && wl_rlen >= 8) {
                    wl_dispatch();
                    if (wl_rlen == 0) break;
                }
            } else if (fd == ctl_fd) {
                ctl_accept();
            } else if (fd == status_tfd) {
                uint64_t exp; (void)!read(status_tfd, &exp, sizeof exp);
                status_tick(tick_n);
                gamma_tick(tick_n);
                tick_n++;
                bar_redraw_all();
            } else if (fd == key_rep_tfd) {
                uint64_t exp; (void)!read(key_rep_tfd, &exp, sizeof exp);
                if (key_rep_key) {
                    Widget *w = widget_by_surface(kbd_focus);
                    if (w && w->kind == W_MENU) menu_on_key(w, key_rep_key, 1);
                    else if (w && w->kind == W_LOCK) lock_on_key(w, key_rep_key, 1, 0);
                    else key_rep_cancel();
                }
            } else if (fd == lock_helper_fd()) {
                lock_on_helper_event();
            } else if (fd == sig_fd) {
                struct signalfd_siginfo si;
                int got_usr1 = 0, got_chld = 0;
                while (read(sig_fd, &si, sizeof si) == (ssize_t)sizeof si) {
                    if (si.ssi_signo == SIGCHLD) got_chld = 1;
                    else                          got_usr1 = 1;
                }
                if (got_chld) hud_on_sigchld();
                if (got_usr1) { status_sample_all(); bar_redraw_all(); }
            } else if (dbus >= 0 && fd == dbus) {
                dbus_dispatch();
            } else {
                Client *c = NULL;
                for (int j = 0; j < MAX_CLIENTS; j++)
                    if (clients[j].fd == fd) { c = &clients[j]; break; }
                if (c) ctl_read(c);
                else   epoll_del_fd(fd);   /* defensive — shouldn't reach */
            }
        }
    }
}
