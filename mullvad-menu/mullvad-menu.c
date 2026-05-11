/* mullvad-menu — ncurses server picker for the dwlarp Mullvad VPN button.
 *
 * Reads the relay list cached by mullvad-wg-setup at /run/mullvad.relays
 * (TSV: hostname, pubkey, ipv4, country_code, country_name, city_code, city_name).
 * Connect/disconnect/reconnect/killswitch toggle happen via
 * `sudo -n /usr/local/bin/mullvad-vpn …`, which is pre-authorised in
 * /etc/sudoers.d/ws-mullvad. Health and killswitch status are read from
 * /run/mullvad.{handshake,killswitch} without privileges (those files are
 * 0644 and maintained by mullvad-watchdog + mullvad-vpn itself). */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../config.h"

/* Relay catalogue is persistent (/var/lib/mullvad/relays.tsv) so it survives
 * reboot — earlier /run path was tmpfs, lost every boot. Session state
 * (current host, handshake age, killswitch state) stays in /run. */
#define RELAYS_FILE     "/var/lib/mullvad/relays.tsv"
#define CURRENT_FILE    "/run/mullvad.current"
#define HANDSHAKE_FILE  "/run/mullvad.handshake"
#define KS_FILE         "/run/mullvad.killswitch"
#define MULLVAD_BIN     "/usr/local/bin/mullvad-vpn"

#define MAX_RELAYS  4096
#define FILTER_MAX  64

struct relay {
	char hostname[40];
	char cc[4];
	char country[40];
	char city_code[8];
	char city[40];
};

static struct relay relays[MAX_RELAYS];
static int n_relays;

/* The synthetic "disconnect" entry sits at index -1 in selection space; we
   render it as the first row above all relays. Using a real index avoids two
   parallel codepaths (one for relay rows, one for the action row). */
#define IDX_DISCONNECT (-1)

static int filter_idx[MAX_RELAYS];
static int n_filter;
static int sel;          /* IDX_DISCONNECT or 0..n_filter-1 */
static int top_visible;

static char q_filter[FILTER_MAX];
static int  filter_len;

static char current_host[40];
static char status_msg[160];
static int  setup_error;  /* 1 if relays file is missing/empty — show help screen */

static int
ci_strstr(const char *hay, const char *ndl)
{
	if (!*ndl) return 1;
	for (; *hay; hay++) {
		const char *h = hay, *n = ndl;
		while (*h && *n &&
		       tolower((unsigned char)*h) == tolower((unsigned char)*n)) {
			h++; n++;
		}
		if (!*n) return 1;
	}
	return 0;
}

static int
relay_cmp(const void *a, const void *b)
{
	const struct relay *r1 = a, *r2 = b;
	int c = strcmp(r1->country, r2->country);
	if (c) return c;
	c = strcmp(r1->city, r2->city);
	if (c) return c;
	return strcmp(r1->hostname, r2->hostname);
}

static void
copy_field(char *dst, size_t cap, const char *src)
{
	size_t l = strlen(src);
	if (l >= cap) l = cap - 1;
	memcpy(dst, src, l);
	dst[l] = '\0';
}

static int
load_relays(void)
{
	FILE *f = fopen(RELAYS_FILE, "r");
	if (!f) return -1;
	char line[1024];
	while (n_relays < MAX_RELAYS && fgets(line, sizeof line, f)) {
		char *p = line, *fields[7] = {0};
		int n = 0;
		fields[n++] = p;
		while (*p && n < 7) {
			if (*p == '\t') { *p = '\0'; fields[n++] = p + 1; }
			p++;
		}
		while (*p) {
			if (*p == '\n' || *p == '\r') { *p = '\0'; break; }
			p++;
		}
		if (n < 7 || !*fields[0]) continue;
		struct relay *r = &relays[n_relays++];
		copy_field(r->hostname,  sizeof r->hostname,  fields[0]);
		copy_field(r->cc,        sizeof r->cc,        fields[3]);
		copy_field(r->country,   sizeof r->country,   fields[4]);
		copy_field(r->city_code, sizeof r->city_code, fields[5]);
		copy_field(r->city,      sizeof r->city,      fields[6]);
		/* Fall back to country code if the cached file pre-dates the
		   country-name field — older mullvad-wg-setup wrote 3-field rows. */
		if (!*r->country) copy_field(r->country, sizeof r->country, r->cc);
	}
	fclose(f);
	return 0;
}

static void
load_current(void)
{
	current_host[0] = '\0';
	FILE *f = fopen(CURRENT_FILE, "r");
	if (!f) return;
	if (fgets(current_host, sizeof current_host, f)) {
		size_t l = strlen(current_host);
		while (l > 0 && (current_host[l-1] == '\n' ||
		                 current_host[l-1] == '\r' ||
		                 current_host[l-1] == ' '))
			current_host[--l] = '\0';
	}
	fclose(f);
}

/* Latest WireGuard handshake age in seconds, or -1 if unknown / never.
   Source of truth is mullvad-watchdog (root) writing /run/mullvad.handshake
   every 5s. Reading is unprivileged. */
static long
handshake_age(void)
{
	FILE *f = fopen(HANDSHAKE_FILE, "r");
	if (!f) return -1;
	long long hs = 0;
	if (fscanf(f, "%lld", &hs) != 1) hs = 0;
	fclose(f);
	if (hs <= 0) return -1;
	long long now = (long long)time(NULL);
	long age = (long)(now - hs);
	return age < 0 ? 0 : age;
}

/* Killswitch state, "on" or "off". Written by mullvad-vpn ks_load/ks_unload
   and refreshed by mullvad-watchdog every 5s. Missing file → "off". */
static const char *
killswitch_status(void)
{
	static char buf[8];
	buf[0] = '\0';
	FILE *f = fopen(KS_FILE, "r");
	if (!f) return "off";
	if (fgets(buf, sizeof buf, f)) {
		size_t l = strlen(buf);
		while (l > 0 && (buf[l-1] == '\n' || buf[l-1] == '\r' || buf[l-1] == ' '))
			buf[--l] = '\0';
	}
	fclose(f);
	return buf[0] ? buf : "off";
}

static void
rebuild_filter(void)
{
	n_filter = 0;
	for (int i = 0; i < n_relays; i++) {
		struct relay *r = &relays[i];
		if (filter_len == 0 ||
		    ci_strstr(r->country,  q_filter) ||
		    ci_strstr(r->city,     q_filter) ||
		    ci_strstr(r->hostname, q_filter) ||
		    ci_strstr(r->cc,       q_filter))
			filter_idx[n_filter++] = i;
	}
}

static void
pick_default_selection(void)
{
	if (current_host[0]) {
		for (int i = 0; i < n_filter; i++)
			if (!strcmp(relays[filter_idx[i]].hostname, current_host)) {
				sel = i; return;
			}
	}
	for (int i = 0; i < n_filter; i++)
		if (!strcmp(relays[filter_idx[i]].cc, "cz")) {
			sel = i; return;
		}
	sel = n_filter > 0 ? 0 : IDX_DISCONNECT;
}

/* fire-and-forget mako/libnotify notification. Double-fork so the grandchild
   reparents to init and we don't have to track its exit. */
static void
notify(const char *summary, const char *body, const char *icon)
{
	pid_t pid = fork();
	if (pid < 0) return;
	if (pid == 0) {
		if (fork() == 0) {
			execlp("notify-send", "notify-send",
			       "-a", "Mullvad",
			       "-i", icon ? icon : "network-vpn",
			       summary,
			       body ? body : "",
			       (char *)NULL);
			_exit(127);
		}
		_exit(0);
	}
	int st;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
		;
}

/* Block while sudo runs so the user sees the result instead of returning to a
   stale screen. ncurses is suspended around the call so the child inherits a
   normal terminal. */
static int
run_blocking(const char *const argv[])
{
	def_prog_mode();
	endwin();

	pid_t pid = fork();
	if (pid < 0) { reset_prog_mode(); refresh(); return -1; }
	if (pid == 0) {
		execv(argv[0], (char *const *)argv);
		_exit(127);
	}
	int st = 0;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
		;

	reset_prog_mode();
	refresh();
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

static int
do_connect(const char *host)
{
	const char *argv[] = { "/usr/bin/sudo", "-n", MULLVAD_BIN, "up", host, NULL };
	int rc = run_blocking(argv);
	load_current();
	if (rc == 0) {
		snprintf(status_msg, sizeof status_msg, "connected to %s", host);
		notify("Mullvad VPN", status_msg, "network-vpn");
	} else {
		snprintf(status_msg, sizeof status_msg, "connect to %s failed (rc=%d)", host, rc);
		notify("Mullvad VPN", status_msg, "network-vpn-disabled");
	}
	return rc;
}

static int
do_disconnect(void)
{
	const char *argv[] = { "/usr/bin/sudo", "-n", MULLVAD_BIN, "down", NULL };
	int rc = run_blocking(argv);
	load_current();
	if (rc == 0) {
		snprintf(status_msg, sizeof status_msg, "disconnected");
		notify("Mullvad VPN", "Disconnected", "network-vpn-disabled");
	} else {
		snprintf(status_msg, sizeof status_msg, "disconnect failed (rc=%d)", rc);
		notify("Mullvad VPN", status_msg, "network-vpn-disabled");
	}
	return rc;
}

/* Force a fresh re-rank+up — used by F5 when the current relay is stale or
   the user wants a snappier one (e.g. after moving networks). */
static int
do_reconnect(void)
{
	const char *argv[] = { "/usr/bin/sudo", "-n", MULLVAD_BIN, "reconnect", NULL };
	int rc = run_blocking(argv);
	load_current();
	if (rc == 0) {
		snprintf(status_msg, sizeof status_msg,
		         "reconnected%s%s", current_host[0] ? " via " : "", current_host);
		notify("Mullvad VPN", status_msg, "network-vpn");
	} else {
		snprintf(status_msg, sizeof status_msg, "reconnect failed (rc=%d)", rc);
		notify("Mullvad VPN", status_msg, "network-vpn-disabled");
	}
	return rc;
}

/* Toggle the nftables killswitch. Stays in the menu so the user can see the
   new state on the next redraw and react (e.g. captive portal recovery). */
static int
do_killswitch_toggle(void)
{
	const char *next = strcmp(killswitch_status(), "on") == 0 ? "off" : "on";
	const char *argv[] = { "/usr/bin/sudo", "-n", MULLVAD_BIN, "killswitch", next, NULL };
	int rc = run_blocking(argv);
	if (rc == 0)
		snprintf(status_msg, sizeof status_msg, "killswitch %s", next);
	else
		snprintf(status_msg, sizeof status_msg, "killswitch toggle failed (rc=%d)", rc);
	return rc;
}

static void
clamp_scroll(int list_height)
{
	int eff = sel == IDX_DISCONNECT ? 0 : sel + 1;
	if (eff < top_visible) top_visible = eff;
	if (eff >= top_visible + list_height) top_visible = eff - list_height + 1;
	if (top_visible < 0) top_visible = 0;
}

static void
draw_setup_help(void)
{
	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	const char *lines[] = {
		"",
		"  Mullvad isn't bootstrapped yet.",
		"",
		"  The relay cache at /etc/wireguard/mullvad.relays is missing or empty.",
		"",
		"  Run this once with your account number to generate the WireGuard",
		"  key, register it with Mullvad, and download the relay list:",
		"",
		"      sudo mullvad-wg-setup <ACCOUNT_NUMBER>",
		"",
		"  Then re-open this menu (the ws-hud shield button).",
		"",
		"  Press Esc or q to close.",
		NULL,
	};
	int n = 0; while (lines[n]) n++;
	int y0 = (rows - n) / 2;
	if (y0 < 0) y0 = 0;
	for (int i = 0; lines[i]; i++) {
		int x = (cols - (int)strlen(lines[i])) / 2;
		if (x < 0) x = 0;
		if (i == 1) attron(A_BOLD);
		mvprintw(y0 + i, x, "%s", lines[i]);
		if (i == 1) attroff(A_BOLD);
	}
}

static void
draw(void)
{
	erase();
	int rows, cols;
	getmaxyx(stdscr, rows, cols);
	if (cols < 30) cols = 30;

	if (setup_error) { draw_setup_help(); refresh(); return; }

	attron(A_BOLD);
	if (current_host[0]) {
		long age = handshake_age();
		if (age < 0)
			mvprintw(0, 0, " mullvad — connected: %s (no handshake yet)", current_host);
		else if (age > WS_VPN_STALE_S)
			mvprintw(0, 0, " mullvad — STALE: %s (last handshake %lds ago)", current_host, age);
		else
			mvprintw(0, 0, " mullvad — connected: %s (handshake %lds ago)", current_host, age);
	} else {
		mvprintw(0, 0, " mullvad — disconnected");
	}
	attroff(A_BOLD);
	/* Killswitch state — right-aligned on the same row. */
	{
		const char *ks = killswitch_status();
		char ks_buf[40];
		snprintf(ks_buf, sizeof ks_buf, "killswitch: %s ", ks);
		int klen = (int)strlen(ks_buf);
		clrtoeol();
		if (cols - klen > 30)
			mvprintw(0, cols - klen, "%s", ks_buf);
	}

	mvprintw(1, 0, " filter: %s", q_filter);
	addch('_');
	clrtoeol();
	mvhline(2, 0, ACS_HLINE, cols);

	int list_top    = 3;
	int list_bottom = rows - 2;
	int list_height = list_bottom - list_top;
	if (list_height < 1) list_height = 1;

	clamp_scroll(list_height);

	int y = list_top;
	for (int eff = top_visible; eff <= n_filter && y < list_bottom; eff++, y++) {
		if (eff == 0) {
			int is_sel = (sel == IDX_DISCONNECT);
			if (is_sel) attron(A_REVERSE);
			mvprintw(y, 0, "   [ disconnect VPN ]");
			clrtoeol();
			if (is_sel) attroff(A_REVERSE);
			continue;
		}
		struct relay *r = &relays[filter_idx[eff - 1]];
		int is_sel = (sel == eff - 1);
		int is_cur = current_host[0] && !strcmp(r->hostname, current_host);
		if (is_sel)      attron(A_REVERSE);
		else if (is_cur) attron(A_BOLD);
		mvprintw(y, 0, " %s [%-2s] %-22s %-15s %s",
		         is_cur ? "*" : " ",
		         r->cc, r->country, r->city, r->hostname);
		clrtoeol();
		if (is_sel)      attroff(A_REVERSE);
		else if (is_cur) attroff(A_BOLD);
	}

	mvhline(rows - 2, 0, ACS_HLINE, cols);
	if (status_msg[0]) {
		attron(A_DIM);
		mvprintw(rows - 2, 2, " %s ", status_msg);
		attroff(A_DIM);
	}
	mvprintw(rows - 1, 0,
		" type:filter  ↑↓:move  enter:connect (or disconnect at top)  F5:reconnect  F2:killswitch  esc:quit");
	clrtoeol();

	refresh();
}

static void
move_sel(int delta)
{
	int eff = sel == IDX_DISCONNECT ? 0 : sel + 1;
	eff += delta;
	if (eff < 0) eff = 0;
	if (eff > n_filter) eff = n_filter;
	sel = (eff == 0) ? IDX_DISCONNECT : eff - 1;
}

int
main(void)
{
	setlocale(LC_ALL, "");
	signal(SIGINT,  SIG_DFL);
	signal(SIGTERM, SIG_DFL);

	if (load_relays() < 0 || n_relays == 0) {
		setup_error = 1;
		notify("Mullvad VPN",
		       "Relay cache missing — run sudo mullvad-wg-setup <ACCOUNT>",
		       "dialog-warning");
	} else {
		qsort(relays, n_relays, sizeof relays[0], relay_cmp);
		load_current();
		rebuild_filter();
		pick_default_selection();
	}

	initscr();
	cbreak();
	noecho();
	keypad(stdscr, TRUE);
	curs_set(0);
	set_escdelay(50);

	for (;;) {
		draw();
		int c = getch();
		if (c == 27 /* ESC */) break;
		if (setup_error) { if (c == 'q') break; else continue; }

		if (c == KEY_UP)              move_sel(-1);
		else if (c == KEY_DOWN)       move_sel(+1);
		else if (c == KEY_PPAGE)      move_sel(-10);
		else if (c == KEY_NPAGE)      move_sel(+10);
		else if (c == KEY_HOME)       sel = IDX_DISCONNECT;
		else if (c == KEY_END)        sel = n_filter > 0 ? n_filter - 1 : IDX_DISCONNECT;
		else if (c == KEY_F(2))       { do_killswitch_toggle(); /* stay open; redraw shows new state */ }
		else if (c == KEY_F(5))       { do_reconnect(); break; }
		else if (c == '\n' || c == KEY_ENTER || c == '\r') {
			if (sel == IDX_DISCONNECT) {
				do_disconnect();
				break;
			} else if (sel >= 0 && sel < n_filter) {
				do_connect(relays[filter_idx[sel]].hostname);
				break;
			}
		}
		else if (c == KEY_BACKSPACE || c == 127 || c == 8) {
			if (filter_len > 0) {
				q_filter[--filter_len] = '\0';
				rebuild_filter();
				if (sel >= n_filter) sel = n_filter > 0 ? n_filter - 1 : IDX_DISCONNECT;
			}
		}
		else if (c >= 32 && c < 127 && filter_len < FILTER_MAX - 1) {
			q_filter[filter_len++] = (char)c;
			q_filter[filter_len]   = '\0';
			rebuild_filter();
			pick_default_selection();
			top_visible = 0;
		}
	}

	endwin();
	return 0;
}
