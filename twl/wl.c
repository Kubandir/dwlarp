/* Wayland wire I/O. */
#include "twl.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/wait.h>

int      wl_fd = -1;
uint32_t wl_next_id = 2;
uint8_t  wl_rbuf[8192];
int      wl_rlen = 0;

static int  pending_fds[16];
static int  n_pending_fds = 0;

uint32_t id_compositor, id_shm, id_seat;
uint32_t id_layer_shell, id_wm_base;
uint32_t id_pointer, id_keyboard;
uint32_t id_dwl_mgr;
uint32_t id_gamma_mgr;
uint32_t id_slock_mgr, id_slock;

Output  outputs[MAX_OUTPUTS];
Output *focused_output;

uint32_t ptr_focus, kbd_focus;
int      ptr_x, ptr_y;
uint32_t enter_serial;

static int  globals_synced = 0;
static uint32_t sync_id = 0;

/* ============================================================ */
/* Output registry                                               */
/* ============================================================ */

int output_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_OUTPUTS; i++) if (outputs[i].active) n++;
    return n;
}

Output *output_alloc(uint32_t registry_name) {
    for (int i = 0; i < MAX_OUTPUTS; i++) {
        if (!outputs[i].active) {
            memset(&outputs[i], 0, sizeof outputs[i]);
            outputs[i].active = 1;
            outputs[i].registry_name = registry_name;
            outputs[i].last_applied_k = 0;
            return &outputs[i];
        }
    }
    return NULL;
}

Output *output_by_wl(uint32_t wl_output) {
    if (!wl_output) return NULL;
    for (int i = 0; i < MAX_OUTPUTS; i++)
        if (outputs[i].active && outputs[i].wl_output == wl_output)
            return &outputs[i];
    return NULL;
}

Output *output_by_ipc(uint32_t dwl_ipc) {
    if (!dwl_ipc) return NULL;
    for (int i = 0; i < MAX_OUTPUTS; i++)
        if (outputs[i].active && outputs[i].dwl_ipc_output == dwl_ipc)
            return &outputs[i];
    return NULL;
}

Output *output_by_gamma(uint32_t gamma_ctrl) {
    if (!gamma_ctrl) return NULL;
    for (int i = 0; i < MAX_OUTPUTS; i++)
        if (outputs[i].active && outputs[i].gamma_ctrl == gamma_ctrl)
            return &outputs[i];
    return NULL;
}

Output *output_by_registry_name(uint32_t name) {
    for (int i = 0; i < MAX_OUTPUTS; i++)
        if (outputs[i].active && outputs[i].registry_name == name)
            return &outputs[i];
    return NULL;
}

static void spawn_autolayout(void) {
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        int devnull = open("/dev/null", O_RDWR);
        if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); close(devnull); }
        execl("/bin/sh", "sh", "-c",
              "exec \"$HOME/.local/bin/dwl-autolayout\"", (char *)NULL);
        _exit(127);
    }
    /* Reaped by twl.c's signalfd SIGCHLD path (hud_on_sigchld waitpids any). */
}

int pad4(int x) { return (x + 3) & ~3; }
uint32_t wl_new_id(void) { return ++wl_next_id; }

void msg(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    fflush(stderr);
    va_end(ap);
}
void die(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fputs("twl: ", stderr); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap); exit(1);
}

void wl_send(const void *buf, unsigned len, int fd) {
    struct iovec iov = { (void *)buf, len };
    if (fd < 0) {
        struct msghdr m = { .msg_iov = &iov, .msg_iovlen = 1 };
        if (sendmsg(wl_fd, &m, MSG_NOSIGNAL) < 0) die("sendmsg: %s", strerror(errno));
        return;
    }
    union { char b[CMSG_SPACE(sizeof(int))]; struct cmsghdr a; } c = {0};
    struct msghdr m = {
        .msg_iov = &iov, .msg_iovlen = 1,
        .msg_control = c.b, .msg_controllen = CMSG_LEN(sizeof(int)),
    };
    struct cmsghdr *h = CMSG_FIRSTHDR(&m);
    h->cmsg_level = SOL_SOCKET; h->cmsg_type = SCM_RIGHTS;
    h->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(h), &fd, sizeof(int));
    if (sendmsg(wl_fd, &m, MSG_NOSIGNAL) < 0) die("sendmsg(fd): %s", strerror(errno));
}

void wl_req(uint32_t obj, uint16_t op, const uint32_t *args, int n, int fd) {
    uint32_t b[32];
    if (n + 2 > (int)(sizeof b / 4)) die("req too large");
    b[0] = obj;
    uint32_t size = 8 + n * 4;
    b[1] = (size << 16) | op;
    for (int i = 0; i < n; i++) b[2 + i] = args[i];
    wl_send(b, size, fd);
}

/* Request with string embedded between pre[] and post[] arg arrays. */
void wl_req_str(uint32_t obj, uint16_t op, const uint32_t *pre, int npre,
                const char *s, const uint32_t *post, int npost) {
    uint32_t b[64];
    int sl = (int)strlen(s) + 1, pl = pad4(sl);
    int p = 2;
    b[0] = obj;
    for (int i = 0; i < npre; i++) b[p++] = pre[i];
    b[p++] = sl;
    memset((char *)&b[p], 0, pl);
    memcpy((char *)&b[p], s, sl);
    p += pl / 4;
    for (int i = 0; i < npost; i++) b[p++] = post[i];
    uint32_t size = p * 4;
    b[1] = (size << 16) | op;
    wl_send(b, size, -1);
}

void wl_registry_bind(uint32_t name, const char *iface, uint32_t version,
                      uint32_t new_oid) {
    uint32_t pre[1] = { name };
    uint32_t post[2] = { version, new_oid };
    wl_req_str(ID_REGISTRY, REGISTRY_REQ_BIND, pre, 1, iface, post, 2);
}

int wl_take_pending_fd(void) {
    if (!n_pending_fds) return -1;
    int fd = pending_fds[0];
    for (int i = 1; i < n_pending_fds; i++) pending_fds[i - 1] = pending_fds[i];
    n_pending_fds--;
    return fd;
}
void wl_close_pending_fds(void) {
    for (int i = 0; i < n_pending_fds; i++) close(pending_fds[i]);
    n_pending_fds = 0;
}

int wl_recv(int block) {
    struct iovec iov = { wl_rbuf + wl_rlen, sizeof wl_rbuf - wl_rlen };
    union { char b[CMSG_SPACE(8 * sizeof(int))]; struct cmsghdr a; } c;
    struct msghdr m = {
        .msg_iov = &iov, .msg_iovlen = 1,
        .msg_control = c.b, .msg_controllen = sizeof c.b,
    };
    ssize_t n = recvmsg(wl_fd, &m, block ? 0 : MSG_DONTWAIT);
    if (n < 0) return errno == EAGAIN ? 0 : -1;
    if (n == 0) return -1;
    wl_rlen += n;
    for (struct cmsghdr *h = CMSG_FIRSTHDR(&m); h; h = CMSG_NXTHDR(&m, h))
        if (h->cmsg_level == SOL_SOCKET && h->cmsg_type == SCM_RIGHTS) {
            int nf = (h->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            int *fds = (int *)CMSG_DATA(h);
            for (int i = 0; i < nf; i++) {
                if (n_pending_fds < (int)(sizeof pending_fds / sizeof *pending_fds))
                    pending_fds[n_pending_fds++] = fds[i];
                else close(fds[i]);
            }
        }
    return 0;
}

void wl_connect(void) {
    const char *dir  = getenv("XDG_RUNTIME_DIR");
    const char *disp = getenv("WAYLAND_DISPLAY");
    if (!dir || !disp) die("XDG_RUNTIME_DIR / WAYLAND_DISPLAY not set");

    struct sockaddr_un a = { .sun_family = AF_UNIX };
    int n = snprintf(a.sun_path, sizeof a.sun_path,
                     disp[0] == '/' ? "%s" : "%s/%s",
                     disp[0] == '/' ? disp : dir,
                     disp[0] == '/' ? ""   : disp);
    if (n <= 0 || n >= (int)sizeof a.sun_path) die("wl path too long");

    wl_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (wl_fd < 0) die("wl socket: %s", strerror(errno));
    if (connect(wl_fd, (struct sockaddr *)&a, sizeof a) < 0)
        die("wl connect %s: %s", a.sun_path, strerror(errno));
    int fl = fcntl(wl_fd, F_GETFL); fcntl(wl_fd, F_SETFL, fl | O_NONBLOCK);

    uint32_t rid = ID_REGISTRY;
    wl_req(ID_DISPLAY, DISPLAY_REQ_GET_REGISTRY, &rid, 1, -1);
    sync_id = wl_new_id();
    uint32_t sa = sync_id;
    wl_req(ID_DISPLAY, DISPLAY_REQ_SYNC, &sa, 1, -1);

    /* Block until sync.done so globals are bound before we do anything. */
    while (!globals_synced) {
        int fl2 = fcntl(wl_fd, F_GETFL); fcntl(wl_fd, F_SETFL, fl2 & ~O_NONBLOCK);
        int rc = wl_recv(1);
        fcntl(wl_fd, F_SETFL, fl2);
        if (rc < 0) die("wl recv: %s", strerror(errno));
        wl_dispatch();
    }
}

/* Forward decls of input handlers in their respective modules. */
extern void on_pointer_event(uint16_t op, uint8_t *body, uint32_t bodylen);
extern void on_keyboard_event(uint16_t op, uint8_t *body, uint32_t bodylen);
extern void on_ls_event(Widget *w, uint16_t op, uint8_t *body, uint32_t bodylen);
extern void on_frame_done(Widget *w, uint32_t cb_id);
extern void on_buffer_release(uint32_t buf_id);

/* Bind the per-output dwl-ipc + gamma sub-objects and spawn the per-output
 * widgets (bar/wall/hud). Called once the global registry has been synced
 * AND we have an Output. For hotplug adds after startup, called immediately. */
void output_init_widgets(Output *o) {
    if (!o || o->widgets_created) return;
    if (!id_compositor || !id_layer_shell) return;   /* prerequisites missing */
    if (id_dwl_mgr && !o->dwl_ipc_output) {
        o->dwl_ipc_output = wl_new_id();
        uint32_t a[2] = { o->dwl_ipc_output, o->wl_output };
        wl_req(id_dwl_mgr, DWL_MGR_REQ_GET_OUTPUT, a, 2, -1);
    }
    if (id_gamma_mgr && !o->gamma_ctrl && !o->gamma_failed)
        gamma_bind_output(o);

    /* The widgets themselves. wall first → it sits on the BACKGROUND layer
     * and we want it painted under any other surface. */
    wall_create_on(o);
    bar_create_on(o);
    hud_create_on(o);
    o->widgets_created = 1;

    /* If a session lock is already in effect, also drop a lock surface on
     * this newly-attached output so it isn't a visible escape hatch. */
    if (lock_active()) lock_on_output_added(o);

    /* If nothing was focused before, default focus to the first output to
     * appear so OSD has somewhere to land before dwl-ipc fires its ACTIVE. */
    if (!focused_output) focused_output = o;
}

/* Compositor-side teardown ordering: layer surfaces are auto-invalidated
 * when their wl_output goes away (and the compositor will send LS_EV_CLOSED
 * ahead of the global_remove). We still need to destroy our object IDs and
 * free the slot. Idempotent. */
void output_destroy(Output *o) {
    if (!o || !o->active) return;

    /* Destroy widgets (sends layer_surface.destroy + surface.destroy). */
    if (o->bar)  { widget_destroy(o->bar);  o->bar  = NULL; }
    if (o->wall) { widget_destroy(o->wall); o->wall = NULL; }
    if (o->hud)  { widget_destroy(o->hud);  o->hud  = NULL; }
    if (o->lock) { lock_on_output_removed(o); }   /* destroys via widget_destroy */

    if (o->gamma_ctrl) {
        wl_req(o->gamma_ctrl, GAMMA_CTRL_REQ_DESTROY, NULL, 0, -1);
        o->gamma_ctrl = 0;
    }
    if (o->dwl_ipc_output) {
        wl_req(o->dwl_ipc_output, DWL_OUT_REQ_RELEASE, NULL, 0, -1);
        o->dwl_ipc_output = 0;
    }
    /* wl_output: no release request in v2; just stop referencing the id. */

    if (focused_output == o) {
        focused_output = NULL;
        for (int i = 0; i < MAX_OUTPUTS; i++)
            if (outputs[i].active && &outputs[i] != o) { focused_output = &outputs[i]; break; }
    }

    /* If the OSD widget was anchored to this output, drop it; next post will
     * re-anchor to whatever is focused now. */
    Widget *osd = widget_first(W_OSD);
    if (osd && osd->output == o) widget_destroy(osd);

    o->active = 0;
}

static void handle_registry_global(uint32_t name, const char *iface, uint32_t ver) {
    if (!id_compositor && !strcmp(iface, "wl_compositor")) {
        id_compositor = wl_new_id(); wl_registry_bind(name, iface, 4, id_compositor);
    } else if (!id_shm && !strcmp(iface, "wl_shm")) {
        id_shm = wl_new_id(); wl_registry_bind(name, iface, 1, id_shm);
    } else if (!id_seat && !strcmp(iface, "wl_seat")) {
        id_seat = wl_new_id(); wl_registry_bind(name, iface, 5, id_seat);
    } else if (!strcmp(iface, "wl_output")) {
        Output *o = output_alloc(name);
        if (!o) { msg("twl: too many outputs (>%d), ignoring extra", MAX_OUTPUTS); return; }
        o->wl_output = wl_new_id();
        wl_registry_bind(name, iface, 2, o->wl_output);
        /* During the startup sync we wait until everything is bound before
         * spawning per-output widgets (output_init_widgets). For post-sync
         * hotplugs, spawn immediately and fire dwl-autolayout. */
        if (globals_synced) {
            output_init_widgets(o);
            spawn_autolayout();
        }
    } else if (!id_layer_shell && !strcmp(iface, "zwlr_layer_shell_v1")) {
        id_layer_shell = wl_new_id();
        wl_registry_bind(name, iface, ver < 4 ? ver : 4, id_layer_shell);
    } else if (!id_wm_base && !strcmp(iface, "xdg_wm_base")) {
        id_wm_base = wl_new_id(); wl_registry_bind(name, iface, 1, id_wm_base);
    } else if (!id_dwl_mgr && !strcmp(iface, "zdwl_ipc_manager_v2")) {
        id_dwl_mgr = wl_new_id();
        wl_registry_bind(name, iface, ver < 2 ? ver : 2, id_dwl_mgr);
    } else if (!id_gamma_mgr && !strcmp(iface, "zwlr_gamma_control_manager_v1")) {
        id_gamma_mgr = wl_new_id();
        wl_registry_bind(name, iface, 1, id_gamma_mgr);
    } else if (!id_slock_mgr && !strcmp(iface, "ext_session_lock_manager_v1")) {
        id_slock_mgr = wl_new_id();
        wl_registry_bind(name, iface, 1, id_slock_mgr);
    }
}

static void on_seat_capabilities(uint32_t caps) {
    if ((caps & SEAT_CAP_POINTER) && !id_pointer) {
        id_pointer = wl_new_id();
        uint32_t a = id_pointer; wl_req(id_seat, SEAT_REQ_GET_POINTER, &a, 1, -1);
    }
    if ((caps & SEAT_CAP_KEYBOARD) && !id_keyboard) {
        id_keyboard = wl_new_id();
        uint32_t a = id_keyboard; wl_req(id_seat, SEAT_REQ_GET_KEYBOARD, &a, 1, -1);
    }
}

static void handle(uint32_t obj, uint16_t op, uint8_t *body, uint32_t bodylen) {
    if (obj == ID_DISPLAY) {
        if (op == DISPLAY_EV_ERROR) {
            uint32_t bad = *(uint32_t *)body;
            uint32_t code = *(uint32_t *)(body + 4);
            uint32_t mlen = *(uint32_t *)(body + 8);
            die("wl error obj=%u code=%u: %.*s", bad, code, (int)mlen, body + 12);
        }
        if (op == DISPLAY_EV_DELETE_ID) {
            /* Server destroyed this object (e.g. dwl_ipc_output when its
             * output went away). Clear any tracked references so we don't
             * send a request on a now-invalid id. */
            uint32_t id = *(uint32_t *)body;
            for (int i = 0; i < MAX_OUTPUTS; i++) {
                Output *o = &outputs[i];
                if (o->dwl_ipc_output == id) o->dwl_ipc_output = 0;
                if (o->gamma_ctrl     == id) o->gamma_ctrl     = 0;
            }
        }
        return;
    }
    if (obj == ID_REGISTRY && op == REGISTRY_EV_GLOBAL) {
        uint32_t name = *(uint32_t *)body;
        uint32_t slen = *(uint32_t *)(body + 4);
        const char *iface = (const char *)(body + 8);
        uint32_t ver = *(uint32_t *)(body + 8 + pad4(slen));
        handle_registry_global(name, iface, ver);
        return;
    }
    if (obj == ID_REGISTRY && op == REGISTRY_EV_GLOBAL_REM) {
        uint32_t name = *(uint32_t *)body;
        Output *o = output_by_registry_name(name);
        if (o) {
            output_destroy(o);
            if (globals_synced) spawn_autolayout();
        }
        return;
    }
    if (obj == sync_id && sync_id && op == CALLBACK_EV_DONE) {
        sync_id = 0; globals_synced = 1; return;
    }
    if (obj == id_wm_base && op == WM_BASE_EV_PING) {
        uint32_t serial = *(uint32_t *)body;
        wl_req(id_wm_base, WM_BASE_REQ_PONG, &serial, 1, -1);
        return;
    }
    if (obj == id_seat && op == SEAT_EV_CAPABILITIES) {
        on_seat_capabilities(*(uint32_t *)body); return;
    }
    if (id_pointer && obj == id_pointer) {
        on_pointer_event(op, body, bodylen); return;
    }
    if (id_keyboard && obj == id_keyboard) {
        on_keyboard_event(op, body, bodylen); return;
    }
    {
        Output *go = output_by_gamma(obj);
        if (go) {
            if (op == GAMMA_CTRL_EV_GAMMA_SIZE) gamma_on_size(go, *(uint32_t *)body);
            else if (op == GAMMA_CTRL_EV_FAILED) gamma_on_failed(go);
            return;
        }
    }
    if (id_slock && obj == id_slock) {
        if (op == SLOCK_EV_LOCKED)        lock_on_locked();
        else if (op == SLOCK_EV_FINISHED) lock_on_finished();
        return;
    }
    {
        Widget *lw = widget_by_slock_surf(obj);
        if (lw && op == SLOCK_SURF_EV_CONFIGURE) {
            uint32_t serial = *(uint32_t *)body;
            uint32_t nw = *(uint32_t *)(body + 4);
            uint32_t nh = *(uint32_t *)(body + 8);
            lock_on_surf_configure(lw, serial, (int)nw, (int)nh);
            return;
        }
    }
    {
        Output *io = output_by_ipc(obj);
        if (io) {
            switch (op) {
            case DWL_OUT_EV_TAG: {
                uint32_t tag     = *(uint32_t *)body;
                uint32_t state   = *(uint32_t *)(body + 4);
                uint32_t clients = *(uint32_t *)(body + 8);
                if (tag < 32) {
                    uint32_t bit = 1u << tag;
                    if (clients)                       io->tag_acc_occ |= bit;
                    if (state == DWL_OUT_TAG_ACTIVE)   io->tag_acc_act |= bit;
                    if (state == DWL_OUT_TAG_URGENT)   io->tag_acc_urg |= bit;
                }
                return;
            }
            case DWL_OUT_EV_FRAME:
                bar_set_tags_on(io, io->tag_acc_occ, io->tag_acc_act, io->tag_acc_urg);
                io->tag_acc_occ = io->tag_acc_act = io->tag_acc_urg = 0;
                return;
            case DWL_OUT_EV_ACTIVE: {
                uint32_t active = *(uint32_t *)body;
                io->is_active = (int)active;
                if (active) focused_output = io;
                return;
            }
            default:
                return;  /* title/appid/layout/visibility — not consumed yet */
            }
        }
    }
    /* Opcode 0 is overloaded across interfaces (layer-surface.configure,
       buffer.release, callback.done, ...). Disambiguate by object id —
       layer-surface and frame-callback first, then buffer release as the
       remaining op==0 destination. */
    Widget *w = widget_by_ls(obj);
    if (w) { on_ls_event(w, op, body, bodylen); return; }
    if (op == CALLBACK_EV_DONE) {
        for (int i = 0; i < MAX_WIDGETS; i++) {
            if (widgets[i].kind != W_NONE && widgets[i].frame_cb == obj) {
                on_frame_done(&widgets[i], obj);
                return;
            }
        }
    }
    if (op == BUFFER_EV_RELEASE) on_buffer_release(obj);
}

/* Drain the read buffer using a read cursor; compact once at the end instead
 * of memmove-after-every-event. dwl tag-update bursts and pointer-motion
 * bursts deliver many small events per packet; the old per-event memmove was
 * O(n²) in bytes moved. */
void wl_dispatch(void) {
    int pos = 0;
    while (wl_rlen - pos >= 8) {
        uint32_t obj  = *(uint32_t *)(wl_rbuf + pos);
        uint32_t hh   = *(uint32_t *)(wl_rbuf + pos + 4);
        uint16_t op   = hh & 0xffff;
        uint16_t size = hh >> 16;
        if (size < 8 || size > sizeof wl_rbuf) die("wl bad size %u", size);
        if (wl_rlen - pos < size) break;
        handle(obj, op, wl_rbuf + pos + 8, size - 8);
        pos += size;
    }
    if (pos > 0) {
        if (wl_rlen - pos > 0)
            memmove(wl_rbuf, wl_rbuf + pos, wl_rlen - pos);
        wl_rlen -= pos;
    }
    wl_close_pending_fds();
}
