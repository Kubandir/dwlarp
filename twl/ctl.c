/* Control socket: SOCK_STREAM at $XDG_RUNTIME_DIR/twl.sock. Commands are
 * tab-separated args, one per line. */
#include "twl.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

Client clients[MAX_CLIENTS];
int    ctl_fd = -1;
char   ctl_path[128];

static Client *client_add(int fd) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (clients[i].fd < 0) {
            clients[i].fd = fd; clients[i].len = 0;
            epoll_add_fd(fd);
            return &clients[i];
        }
    return NULL;
}
static void client_close(Client *c) {
    if (c->fd >= 0) { epoll_del_fd(c->fd); close(c->fd); }
    c->fd = -1; c->len = 0;
}

void ctl_open(void) {
    const char *dir = getenv("XDG_RUNTIME_DIR");
    if (!dir) die("XDG_RUNTIME_DIR not set");
    int n = snprintf(ctl_path, sizeof ctl_path, "%s/%s", dir, CTL_SOCK_NAME);
    if (n <= 0 || n >= (int)sizeof ctl_path) die("ctl path too long");
    unlink(ctl_path);
    ctl_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (ctl_fd < 0) die("ctl socket: %s", strerror(errno));
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    strncpy(a.sun_path, ctl_path, sizeof a.sun_path - 1);
    if (bind(ctl_fd, (struct sockaddr *)&a, sizeof a) < 0)
        die("ctl bind: %s", strerror(errno));
    if (listen(ctl_fd, 8) < 0) die("ctl listen: %s", strerror(errno));
    for (int i = 0; i < MAX_CLIENTS; i++) clients[i].fd = -1;
}

void ctl_close(void) {
    if (ctl_path[0]) unlink(ctl_path);
    if (ctl_fd >= 0) close(ctl_fd);
}

static int split_tab(char *s, char **argv, int maxv) {
    int n = 0;
    while (n < maxv) {
        argv[n++] = s;
        char *p = strchr(s, '\t');
        if (!p) break;
        *p = 0; s = p + 1;
    }
    return n;
}

static int parse_hex(const char *s, unsigned *out) {
    unsigned v = 0; int any = 0;
    while (*s == ' ') s++;
    if (s[0] == '#') s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (*s) {
        char c = *s++; int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = 10 + c - 'a';
        else if (c >= 'A' && c <= 'F') d = 10 + c - 'A';
        else break;
        v = (v << 4) | d; any = 1;
    }
    if (!any) return -1;
    *out = v; return 0;
}

/* Returns 1 if client should be kept open (deferred reply by widget), 0 close.
 * argv[] is sized for the menu command (which carries up to MAX_ITEMS items);
 * non-menu commands never use beyond the first few slots. */
static int dispatch(Client *c, char *cmd) {
    char *argv[MAX_ITEMS + 4];
    int argc = split_tab(cmd, argv, sizeof argv / sizeof *argv);
    if (!argc) return 0;
    const char *op = argv[0];

    if (!strcmp(op, "ping")) {
        (void)!write(c->fd, "pong\n", 5); return 0;
    }
    if (!strcmp(op, "quit")) {
        (void)!write(c->fd, "ok\n", 3);
        ctl_close();
        exit(0);
    }
    if (!strcmp(op, "bar")) {
        if (argc < 2) goto err;
        const char *sub = argv[1];
        if (!strcmp(sub, "title")) {
            bar_set_title(argc >= 3 ? argv[2] : "");
            (void)!write(c->fd, "ok\n", 3); return 0;
        }
        if (!strcmp(sub, "tags") && argc >= 5) {
            unsigned occ, act, urg;
            if (parse_hex(argv[2], &occ) || parse_hex(argv[3], &act) || parse_hex(argv[4], &urg))
                goto err;
            bar_set_tags(occ, act, urg);
            (void)!write(c->fd, "ok\n", 3); return 0;
        }
        if (!strcmp(sub, "refresh")) {
            bar_redraw_all();
            (void)!write(c->fd, "ok\n", 3); return 0;
        }
        goto err;
    }
    if (!strcmp(op, "menu") || !strcmp(op, "hud-cancel") || !strcmp(op, "menu-cancel")) {
        if (!strcmp(op, "hud-cancel") || !strcmp(op, "menu-cancel")) {
            menu_cancel_all();
            (void)!write(c->fd, "ok\n", 3); return 0;
        }
        if (argc < 3) goto err;
        const char *title = argv[1];
        int n = argc - 2;
        if (n > MAX_ITEMS) n = MAX_ITEMS;
        /* 40 KB scratch — declared inside the branch so non-menu commands
         * (bar tags, osd, notify, ...) don't reserve it in the stack frame. */
        char items[MAX_ITEMS][ITEM_MAX];
        for (int i = 0; i < n; i++) {
            size_t l = strnlen(argv[2 + i], ITEM_MAX - 1);
            memcpy(items[i], argv[2 + i], l); items[i][l] = 0;
        }
        Widget *w = menu_create(title[0] ? title : NULL, items, n, c->fd);
        if (!w) { (void)!write(c->fd, "err\n", 4); return 0; }
        return 1;  /* fd handed off; reply on pick/cancel */
    }
    if (!strcmp(op, "hud")) {
        /* Hover-revealed HUD is always live; this command exists to force-probe states. */
        if (argc >= 2 && !strcmp(argv[1], "probe")) {
            hud_probe_states();
            Widget *h = widget_first(W_HUD);
            if (h && h->s.hud.visible) hud_render(h, (int)h->s.hud.cur_oy);
            (void)!write(c->fd, "ok\n", 3); return 0;
        }
        goto err;
    }
    /* osd <slot> <summary> [progress] [icon-cp] [muted0/1]
     *   slot: small uint, used as replace_id so successive presses replace
     *   progress: -1 omits the bar
     *   icon-cp: hex nerd-font codepoint (e.g. f028 for volume)
     *   muted: 1 → red styling */
    if (!strcmp(op, "osd")) {
        if (argc < 3) goto err;
        unsigned slot = 0; parse_hex(argv[1], &slot);
        const char *summary = argv[2];
        int progress = argc >= 4 ? atoi(argv[3]) : -1;
        unsigned icon = 0;
        if (argc >= 5) parse_hex(argv[4], &icon);
        int muted = argc >= 6 ? atoi(argv[5]) : 0;
        osd_post(slot ? slot : 0, summary, "", icon, progress, 0, muted, OSD_TIMEOUT_OSD);
        (void)!write(c->fd, "ok\n", 3); return 0;
    }
    /* notify <urgency:0|1|2> <summary> [body] [icon-cp] [timeout-ms]
     *   timeout: -1 → urgency default; 0 → sticky. */
    if (!strcmp(op, "notify")) {
        if (argc < 3) goto err;
        int urgency = atoi(argv[1]);
        const char *summary = argv[2];
        const char *body = argc >= 4 ? argv[3] : "";
        unsigned icon = 0;
        if (argc >= 5) parse_hex(argv[4], &icon);
        int timeout = argc >= 6 ? atoi(argv[5]) : -1;
        osd_post(0, summary, body, icon, -1, urgency, 0, timeout);
        (void)!write(c->fd, "ok\n", 3); return 0;
    }
    if (!strcmp(op, "osd-clear")) {
        osd_close_all();
        (void)!write(c->fd, "ok\n", 3); return 0;
    }
    /* Built-in power menu — absorbed ws-powermenu script. Entries+actions
     * come from POWERMENU_INIT in config.h; icons are pre-baked. */
    if (!strcmp(op, "powermenu")) {
        struct { uint32_t icon; const char *label; const char *cmd; }
            entries[] = POWERMENU_INIT;
        int n = (int)(sizeof entries / sizeof *entries);
        char items[MAX_ITEMS][ITEM_MAX], cmds[MAX_ITEMS][ITEM_MAX];
        for (int i = 0; i < n; i++) {
            /* "<icon-utf8> <label>" — emit the codepoint in UTF-8 ourselves
             * rather than depend on a helper, since the encoding is trivial. */
            uint32_t cp = entries[i].icon;
            char ic[8]; int il = 0;
            if (cp < 0x80) { ic[il++] = cp; }
            else if (cp < 0x800) {
                ic[il++] = 0xc0 | (cp >> 6);
                ic[il++] = 0x80 | (cp & 0x3f);
            } else if (cp < 0x10000) {
                ic[il++] = 0xe0 | (cp >> 12);
                ic[il++] = 0x80 | ((cp >> 6) & 0x3f);
                ic[il++] = 0x80 | (cp & 0x3f);
            } else {
                ic[il++] = 0xf0 | (cp >> 18);
                ic[il++] = 0x80 | ((cp >> 12) & 0x3f);
                ic[il++] = 0x80 | ((cp >> 6) & 0x3f);
                ic[il++] = 0x80 | (cp & 0x3f);
            }
            ic[il] = 0;
            snprintf(items[i], ITEM_MAX, "%s  %s", ic, entries[i].label);
            snprintf(cmds[i],  ITEM_MAX, "%s",     entries[i].cmd);
        }
        menu_create_action("power:", items, cmds, n);
        (void)!write(c->fd, "ok\n", 3); return 0;
    }
    /* media controls — absorbed dwl-osd. */
    if (!strcmp(op, "volume")) {
        media_volume(argc >= 2 ? argv[1] : "");
        (void)!write(c->fd, "ok\n", 3); return 0;
    }
    if (!strcmp(op, "mic")) {
        media_mic(argc >= 2 ? argv[1] : "");
        (void)!write(c->fd, "ok\n", 3); return 0;
    }
    if (!strcmp(op, "backlight")) {
        media_backlight(argc >= 2 ? argv[1] : "");
        (void)!write(c->fd, "ok\n", 3); return 0;
    }
    /* gamma auto|day|night|flat|off|state|is-warm */
    if (!strcmp(op, "gamma")) {
        if (argc < 2) goto err;
        const char *sub = argv[1];
        if (!strcmp(sub, "auto"))       gamma_set_mode(GM_AUTO);
        else if (!strcmp(sub, "day"))   gamma_set_mode(GM_DAY);
        else if (!strcmp(sub, "night")) gamma_set_mode(GM_NIGHT);
        else if (!strcmp(sub, "flat"))  gamma_set_mode(GM_FLAT);
        else if (!strcmp(sub, "off"))   gamma_set_mode(GM_OFF);
        else if (!strcmp(sub, "state")) {
            const char *s = gamma_mode_str();
            (void)!write(c->fd, s, strlen(s));
            (void)!write(c->fd, "\n", 1);
            return 0;
        }
        else if (!strcmp(sub, "is-warm")) {
            /* exit 0 if warm, 1 otherwise — HUD state_cmd convention */
            const char *s = gamma_is_warm() ? "1\n" : "0\n";
            (void)!write(c->fd, s, 2);
            return 0;
        }
        else goto err;
        (void)!write(c->fd, "ok\n", 3);
        return 0;
    }
    /* lock — engage session lock. Returns "ok" once requested; the compositor
     * confirms via locked event. Idempotent if already locked. */
    if (!strcmp(op, "lock")) {
        lock_engage();
        (void)!write(c->fd, "ok\n", 3);
        return 0;
    }
    /* dnd on|off|toggle|status — when on, dbus app notifications (urgency<2)
     * are swallowed. Critical urgency=2 always passes through. */
    if (!strcmp(op, "dnd")) {
        if (argc < 2) goto err;
        const char *sub = argv[1];
        if (!strcmp(sub, "on"))      dnd_on = 1;
        else if (!strcmp(sub, "off")) dnd_on = 0;
        else if (!strcmp(sub, "toggle")) dnd_on = !dnd_on;
        else if (!strcmp(sub, "status")) {
            (void)!write(c->fd, dnd_on ? "on\n" : "off\n", dnd_on ? 3 : 4);
            return 0;
        }
        else goto err;
        (void)!write(c->fd, "ok\n", 3);
        return 0;
    }
err:
    (void)!write(c->fd, "err\n", 4);
    return 0;
}

void ctl_read(Client *c) {
    for (;;) {
        ssize_t n = recv(c->fd, c->buf + c->len, sizeof c->buf - 1 - c->len, 0);
        if (n < 0) {
            if (errno == EAGAIN) return;
            client_close(c); return;
        }
        if (n == 0) { client_close(c); return; }
        c->len += n;
        c->buf[c->len] = 0;
        char *nl = memchr(c->buf, '\n', c->len);
        if (!nl) {
            if (c->len >= (int)sizeof c->buf - 1) client_close(c);
            return;
        }
        *nl = 0;
        if (dispatch(c, c->buf) == 0) client_close(c);
        else {
            /* fd handed off to a widget (menu); it owns lifecycle now. The
             * widget replies synchronously and doesn't read further events,
             * so detach from epoll to avoid spurious wakeups on peer EOF. */
            epoll_del_fd(c->fd);
            c->fd = -1; c->len = 0;
        }
        return;
    }
}

void ctl_accept(void) {
    for (;;) {
        int fd = accept4(ctl_fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (fd < 0) {
            if (errno == EAGAIN) return;
            msg("ctl accept: %s", strerror(errno));
            return;
        }
        if (!client_add(fd)) close(fd);
    }
}
