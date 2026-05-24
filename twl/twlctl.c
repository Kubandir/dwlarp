/* twlctl — client for the twl control socket.
 *   twlctl ping
 *   twlctl bar title <text>            (no-op; bar doesn't show titles)
 *   twlctl bar tags <occ> <act> <urg>  (hex bitmasks)
 *   twlctl bar refresh
 *   twlctl menu <title> <item1> [...]  (blocks; replies "<idx>\t<text>")
 *   twlctl menu-cancel
 *   twlctl hud probe                   (re-probe HUD button states)
 *   twlctl quit */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

static int connect_daemon(void) {
    const char *dir = getenv("XDG_RUNTIME_DIR");
    if (!dir) { fprintf(stderr, "twlctl: XDG_RUNTIME_DIR not set\n"); return -1; }
    struct sockaddr_un a = { .sun_family = AF_UNIX };
    if (snprintf(a.sun_path, sizeof a.sun_path, "%s/twl.sock", dir)
        >= (int)sizeof a.sun_path) return -1;
    int s = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (s < 0) { perror("socket"); return -1; }
    if (connect(s, (struct sockaddr *)&a, sizeof a) < 0) {
        fprintf(stderr, "twlctl: connect %s: %s\n", a.sun_path, strerror(errno));
        close(s); return -1;
    }
    return s;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fputs("usage: twlctl <cmd> [args...]\n", stderr);
        return 2;
    }
    char msg[16384];
    int n = 0;
    for (int i = 1; i < argc; i++) {
        int r = snprintf(msg + n, sizeof msg - n,
                         i == 1 ? "%s" : "\t%s", argv[i]);
        if (r < 0 || r >= (int)(sizeof msg - n)) {
            fprintf(stderr, "twlctl: command too long\n"); return 1;
        }
        n += r;
    }
    if (n + 1 >= (int)sizeof msg) return 1;
    msg[n++] = '\n';

    int s = connect_daemon();
    if (s < 0) return 1;
    if (send(s, msg, n, MSG_NOSIGNAL) < 0) { perror("send"); close(s); return 1; }

    char rep[256];
    int r = 0;
    for (;;) {
        ssize_t k = recv(s, rep + r, sizeof rep - 1 - r, 0);
        if (k < 0) { if (errno == EINTR) continue; perror("recv"); close(s); return 1; }
        if (k == 0) break;
        r += k;
        if (memchr(rep, '\n', r)) break;
        if (r >= (int)sizeof rep - 1) break;
    }
    close(s);
    if (r == 0) return 1;
    rep[r] = 0;
    char *nl = strchr(rep, '\n'); if (nl) *nl = 0;
    fputs(rep, stdout); fputc('\n', stdout);

    if (!strcmp(argv[1], "menu") || !strcmp(argv[1], "menu-cancel")) {
        int idx = atoi(rep);
        return idx < 0 ? 1 : 0;
    }
    /* `dnd status` → exit 0 if DnD active (mirrors HUD probe contract). */
    if (argc >= 3 && !strcmp(argv[1], "dnd") && !strcmp(argv[2], "status"))
        return strcmp(rep, "on") == 0 ? 0 : 1;
    /* `gamma is-warm` → exit 0 if currently warming the screen (HUD probe). */
    if (argc >= 3 && !strcmp(argv[1], "gamma") && !strcmp(argv[2], "is-warm"))
        return strcmp(rep, "1") == 0 ? 0 : 1;
    return strcmp(rep, "ok") == 0 || strcmp(rep, "pong") == 0 ? 0 : 1;
}
