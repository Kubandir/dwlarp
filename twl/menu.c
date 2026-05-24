/* Menu — dmenu-wl clone. Horizontal top bar:
 *
 *   [ run: query_  | item1  item2  [selected]  item3 ... ]
 *
 * Full-width anchored layer surface, MENU_HEIGHT tall. Type-to-filter,
 * Left/Right to navigate, Enter picks, Esc cancels. Items past the right
 * edge are scrolled in as the selection moves. Reply (over the original
 * control fd): "<idx>\t<text>\n" or "-1\t\n" on cancel. */

#include "twl.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Action-menu state: when set, pick fires action_cmds[i] instead of writing
 * to client_fd. Only one menu is ever live at a time (menu_cancel_all in
 * menu_create), so a single static table is sufficient. */
static char action_cmds[MAX_ITEMS][ITEM_MAX];
static int  action_set;

/* Linux input keycodes (evdev). */
#define KEY_ESC    1
#define KEY_BS    14
#define KEY_TAB   15
#define KEY_ENTER 28
#define KEY_LSHIFT 42
#define KEY_RSHIFT 54
#define KEY_LEFT  105
#define KEY_RIGHT 106
#define KEY_UP    103
#define KEY_DOWN  108
#define KEY_HOME  102
#define KEY_END   107

/* Per-key translation comes from xkb.c (xkb_xlat), driven by the wl_keyboard
 * keymap + modifier events. Shift state is read from xkb_shift_on directly. */

/* Case-insensitive substring match. */
static int item_matches(const char *item, const char *q) {
    if (!q[0]) return 1;
    int qn = strlen(q);
    for (const char *p = item; *p; p++) {
        int k;
        for (k = 0; k < qn && p[k]; k++) {
            char a = p[k], b = q[k];
            if (a >= 'A' && a <= 'Z') a += 32;
            if (b >= 'A' && b <= 'Z') b += 32;
            if (a != b) break;
        }
        if (k == qn) return 1;
    }
    return 0;
}

static void rebuild_filtered(Widget *w) {
    int n = 0;
    for (int i = 0; i < w->s.menu.n_items && n < MAX_ITEMS; i++)
        if (item_matches(w->s.menu.items[i], w->s.menu.query))
            w->s.menu.filtered[n++] = i;
    w->s.menu.n_filtered = n;
    if (w->s.menu.sel >= n) w->s.menu.sel = n - 1;
    if (w->s.menu.sel < 0)  w->s.menu.sel = 0;
}

/* Width of one item's slab (text + 2× padding). */
static int item_slab_w(const Font *f, const char *text) {
    return text_width(f, text) + 2 * MENU_ITEM_PAD_X;
}

void menu_render(Widget *w) {
    if (!w->configured) return;
    widget_ensure_pool(w, 1);
    BufSlot *s = widget_free_slot(w);
    if (!s) return;

    const Font *f = &font_small;
    int W = w->w, H = w->h;
    clear_buf(s->px, W, H, MENU_BG);

    int y = (H - f->line_h) / 2;
    int x = MENU_PAD_X;

    /* prompt + query — query gets a faux cursor "_" so the field looks live */
    char inp[256];
    snprintf(inp, sizeof inp, "%s %s_", MENU_PROMPT, w->s.menu.query);
    draw_text(s->px, W, H, x, y, f, inp, MENU_FG);
    x += text_width(f, inp) + MENU_GAP;

    /* scroll: keep selection on-screen by sliding the items strip leftward */
    int items_x0 = x;
    int items_right = W - MENU_PAD_X;
    int avail = items_right - items_x0;
    if (avail <= 0) { widget_attach(w, s, 0); return; }

    /* compute selection's left/right within the un-scrolled items strip */
    int sel_left = 0, sel_w = 0;
    int total_w = 0;
    for (int i = 0; i < w->s.menu.n_filtered; i++) {
        int iw = item_slab_w(f, w->s.menu.items[w->s.menu.filtered[i]]);
        if (i == w->s.menu.sel) { sel_left = total_w; sel_w = iw; }
        total_w += iw;
    }
    int scroll = 0;
    if (sel_left + sel_w > avail) scroll = (sel_left + sel_w) - avail;
    if (scroll > sel_left) scroll = sel_left;

    /* render items left→right with horizontal scroll applied */
    int cx = items_x0 - scroll;
    for (int i = 0; i < w->s.menu.n_filtered; i++) {
        const char *text = w->s.menu.items[w->s.menu.filtered[i]];
        int iw = item_slab_w(f, text);
        if (cx + iw < items_x0) { cx += iw; continue; }  /* fully off-left */
        if (cx >= items_right) break;                     /* fully off-right */
        uint32_t fg = MENU_FG;
        if (i == w->s.menu.sel) {
            fill_rect(s->px, W, H, cx, 0, iw, H, MENU_SEL_BG);
            fg = MENU_SEL_FG;
        }
        draw_text(s->px, W, H, cx + MENU_ITEM_PAD_X, y, f, text, fg);
        cx += iw;
    }

    widget_attach(w, s, 0);
}

/* Click→pick: walk the same scrolled item layout used by menu_render. */
void menu_on_click(Widget *w, int px_x) {
    const Font *f = &font_small;
    int W = w->w;
    char inp[256];
    snprintf(inp, sizeof inp, "%s %s_", MENU_PROMPT, w->s.menu.query);
    int items_x0 = MENU_PAD_X + text_width(f, inp) + MENU_GAP;
    int items_right = W - MENU_PAD_X;
    int avail = items_right - items_x0;
    if (avail <= 0) return;
    int sel_left = 0, sel_w = 0, total_w = 0;
    for (int i = 0; i < w->s.menu.n_filtered; i++) {
        int iw = item_slab_w(f, w->s.menu.items[w->s.menu.filtered[i]]);
        if (i == w->s.menu.sel) { sel_left = total_w; sel_w = iw; }
        total_w += iw;
    }
    int scroll = 0;
    if (sel_left + sel_w > avail) scroll = (sel_left + sel_w) - avail;
    if (scroll > sel_left) scroll = sel_left;
    int cx = items_x0 - scroll;
    for (int i = 0; i < w->s.menu.n_filtered; i++) {
        int iw = item_slab_w(f, w->s.menu.items[w->s.menu.filtered[i]]);
        if (px_x >= cx && px_x < cx + iw && cx + iw > items_x0 && cx < items_right) {
            menu_reply_and_close(w, w->s.menu.filtered[i]);
            return;
        }
        cx += iw;
    }
}

void menu_reply_and_close(Widget *w, int picked) {
    if (w->client_fd >= 0) {
        char buf[ITEM_MAX + 32];
        int n;
        if (picked >= 0 && picked < w->s.menu.n_items)
            n = snprintf(buf, sizeof buf, "%d\t%s\n", picked, w->s.menu.items[picked]);
        else
            n = snprintf(buf, sizeof buf, "-1\t\n");
        (void)!write(w->client_fd, buf, n);
        close(w->client_fd);
        w->client_fd = -1;
    } else if (action_set && picked >= 0 && picked < w->s.menu.n_items
               && action_cmds[picked][0]) {
        /* Internal action menu: fire the per-item shell command. Fork+exec
         * so the action survives this widget's destruction (e.g. `pkill dwl`
         * for logout, which would otherwise kill the pid running it). */
        pid_t pid = fork();
        if (pid == 0) {
            setsid();
            int devnull = open("/dev/null", O_RDWR);
            if (devnull >= 0) { dup2(devnull, 0); dup2(devnull, 1); dup2(devnull, 2); close(devnull); }
            execl("/bin/sh", "sh", "-c", action_cmds[picked], (char *)NULL);
            _exit(127);
        }
    }
    action_set = 0;
    widget_destroy(w);
}

void menu_cancel_all(void) {
    for (int i = 0; i < MAX_WIDGETS; i++)
        if (widgets[i].kind == W_MENU) menu_reply_and_close(&widgets[i], -1);
}

void menu_on_key(Widget *w, uint32_t key, uint32_t state) {
    if (state == 0) return;
    if (key == KEY_LSHIFT || key == KEY_RSHIFT) return;
    if (key == KEY_ESC) { menu_reply_and_close(w, -1); return; }
    if (key == KEY_ENTER) {
        int picked = w->s.menu.n_filtered > 0
                   ? w->s.menu.filtered[w->s.menu.sel] : -1;
        menu_reply_and_close(w, picked); return;
    }
    /* horizontal nav + arrow fallbacks for muscle memory */
    if (key == KEY_RIGHT || key == KEY_DOWN || key == KEY_TAB) {
        if (w->s.menu.sel + 1 < w->s.menu.n_filtered) w->s.menu.sel++;
        menu_render(w); return;
    }
    if (key == KEY_LEFT  || key == KEY_UP) {
        if (w->s.menu.sel > 0) w->s.menu.sel--;
        menu_render(w); return;
    }
    if (key == KEY_HOME) { w->s.menu.sel = 0; menu_render(w); return; }
    if (key == KEY_END)  { w->s.menu.sel = w->s.menu.n_filtered ? w->s.menu.n_filtered - 1 : 0; menu_render(w); return; }
    if (key == KEY_BS) {
        if (w->s.menu.query_len > 0) {
            int nl = utf8_back(w->s.menu.query, w->s.menu.query_len);
            w->s.menu.query_len = nl;
            w->s.menu.query[nl] = 0;
            rebuild_filtered(w); menu_render(w);
        }
        return;
    }
    uint32_t cp = xkb_xlat(key, xkb_shift_on);
    if (!cp || cp < 0x20 || cp == 0x7f) return;
    char enc[4];
    int n = utf8_encode(cp, enc);
    if (n <= 0) return;
    if (w->s.menu.query_len + n >= (int)sizeof w->s.menu.query) return;
    memcpy(w->s.menu.query + w->s.menu.query_len, enc, n);
    w->s.menu.query_len += n;
    w->s.menu.query[w->s.menu.query_len] = 0;
    rebuild_filtered(w); menu_render(w);
}

Widget *menu_create(const char *title, char items[][ITEM_MAX], int n, int client_fd) {
    (void)title;
    if (n <= 0) return NULL;
    if (n > MAX_ITEMS) n = MAX_ITEMS;
    menu_cancel_all();
    action_set = 0;       /* socket-reply mode; no per-item actions */
    Widget *w = widget_alloc(W_MENU);
    if (!w) return NULL;
    w->client_fd = client_fd;
    w->s.menu.n_items = n;
    w->s.menu.sel = 0;
    for (int i = 0; i < n; i++) {
        size_t l = strnlen(items[i], ITEM_MAX - 1);
        memcpy(w->s.menu.items[i], items[i], l);
        w->s.menu.items[i][l] = 0;
        w->s.menu.filtered[i] = i;
    }
    w->s.menu.n_filtered = n;
    w->s.menu.query[0] = 0;
    w->s.menu.query_len = 0;
    w->s.menu.mods = 0;

    /* Menu sits on the user's current monitor — wherever dwl says focus is. */
    Output *o = focused_output;
    if (!o) {
        for (int i = 0; i < MAX_OUTPUTS; i++)
            if (outputs[i].active) { o = &outputs[i]; break; }
    }
    widget_setup_surface(w, LAYER_OVERLAY, "twl-menu", o);
    widget_set_size(w, 0, MENU_HEIGHT);
    widget_set_anchor(w, LS_ANCHOR_TOP | LS_ANCHOR_LEFT | LS_ANCHOR_RIGHT);
    /* exclusive_zone = -1 → sit at y=0 ignoring the bar's claim, so the menu
       visually replaces the bar instead of stacking under it. */
    widget_set_exclusive_zone(w, -1);
    widget_set_kbd_interactive(w, 1);
    wl_req(w->surface, SURFACE_REQ_COMMIT, NULL, 0, -1);
    return w;
}

/* Built-in action menu: each item carries a shell command run on pick.
 * Used by `twlctl powermenu` (and any future built-in selector). */
Widget *menu_create_action(const char *title,
                           char items[][ITEM_MAX], char actions[][ITEM_MAX],
                           int n) {
    Widget *w = menu_create(title, items, n, -1);
    if (!w) return NULL;
    for (int i = 0; i < n; i++) {
        size_t l = strnlen(actions[i], ITEM_MAX - 1);
        memcpy(action_cmds[i], actions[i], l);
        action_cmds[i][l] = 0;
    }
    action_set = 1;
    return w;
}
