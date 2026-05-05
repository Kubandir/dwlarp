/* dwlb-status — minimal-CPU status feeder for dwlb.
 *
 * One 1-second timerfd is the *sole* render driver. Slow metrics use
 * subdivided cadences:
 *     CPU, brightness   every  1 s
 *     wifi              every  5 s
 *     battery           every 30 s
 *     disk              every  5 min
 *
 * PulseAudio subscriptions update a cached volume — they never render.
 * The next tick paints the new value.  No inotify, no forks, no busy loops.
 * Idle CPU is unmeasurable; under sustained brightness/volume key-mash CPU
 * stays flat because input does not drive output.
 *
 * Build:  cc -O2 -Wall $(pkg-config --cflags libpulse) \
 *            -o dwlb-status dwlb-status.c $(pkg-config --libs libpulse)
 */

#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include <pulse/pulseaudio.h>

/* ---------- nerd-font icons ---------- */
#define I_DISK      "\xf3\xb0\x8b\x8a"
#define I_CPU       "\xef\x92\xbc"
#define I_TEMP      "\xf3\xb0\x88\xb8"   /* nf-md-fire U+F0238 */
#define I_BAT_FULL  "\xef\x89\x80"
#define I_BAT_75    "\xef\x89\x81"
#define I_BAT_50    "\xef\x89\x82"
#define I_BAT_25    "\xef\x89\x83"
#define I_BAT_EMPTY "\xef\x89\x84"
#define I_BAT_CHG   "\xf3\xb0\x82\x84"
#define I_VOL_HI    "\xf3\xb0\x95\xbe"
#define I_VOL_LO    "\xf3\xb0\x95\xbf"
#define I_VOL_OFF   "\xf3\xb0\x9d\x9f"
#define I_WIFI_OFF  "\xf3\xb0\xa4\xab"

static const char *I_BRI[7] = {
	"\xf3\xb0\x83\x99","\xf3\xb0\x83\x9a","\xf3\xb0\x83\x9b",
	"\xf3\xb0\x83\x9c","\xf3\xb0\x83\x9d","\xf3\xb0\x83\x9e",
	"\xf3\xb0\x83\x9f"
};
static const char *I_WIFI[4] = {
	"\xf3\xb0\xa4\x9f","\xf3\xb0\xa4\xa2",
	"\xf3\xb0\xa4\xa5","\xf3\xb0\xa4\xa8"
};

#define FG  "#ffffff"
#define SEP "#7a808b"

/* ---------- cached state ---------- */
static int  cached_cpu_t   = 0;        /* CPU% × 10 */
static int  cached_cpu_temp = -1;      /* °C, -1 = unknown */
static int  cached_disk    = 0;
static int  cached_bat_pct = -1;
static int  cached_bat_chg = 0;
static int  cached_bri_idx = 3;
static const char *cached_wifi_icon = I_WIFI_OFF;
static int  cached_vol     = -1;
static int  cached_muted   = 0;

static char bat_dev[256] = "";
static char bri_dev[256] = "";
static char cpu_temp_path[320] = "";   /* full path to coretemp Package id 0 */

static long long prev_busy, prev_total;

/* ---------- helpers ---------- */
static int read_int_file(const char *p) {
	FILE *f = fopen(p, "r"); if (!f) return -1;
	int v = -1; if (fscanf(f, "%d", &v) != 1) v = -1;
	fclose(f); return v;
}

/* ---------- samplers (mutate cache, no output) ---------- */
static void sample_cpu(void) {
	FILE *f = fopen("/proc/stat", "r"); if (!f) return;
	long long u, n, s, idle, iow=0, irq=0, sirq=0, st=0, gu=0, gn=0;
	int got = fscanf(f, "cpu %lld %lld %lld %lld %lld %lld %lld %lld %lld %lld",
	                 &u,&n,&s,&idle,&iow,&irq,&sirq,&st,&gu,&gn);
	fclose(f);
	if (got < 4) return;
	long long busy  = u + n + s;
	long long total = busy + idle + iow + irq + sirq + st + gu + gn;
	long long db = busy - prev_busy, dt = total - prev_total;
	prev_busy = busy; prev_total = total;
	cached_cpu_t = dt > 0 ? (int)(db * 1000 / dt) : 0;
}

/* Find coretemp's "Package id 0" once. Falls back to temp1_input under the
 * coretemp hwmon dir if no labelled package sensor is found. */
static void detect_cpu_temp(void) {
	if (cpu_temp_path[0]) return;
	DIR *d = opendir("/sys/class/hwmon"); if (!d) return;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char np[320], name[64] = "";
		snprintf(np, sizeof np, "/sys/class/hwmon/%s/name", e->d_name);
		FILE *f = fopen(np, "r");
		if (!f) continue;
		if (!fgets(name, sizeof name, f)) { fclose(f); continue; }
		fclose(f);
		name[strcspn(name, "\n")] = 0;
		if (strcmp(name, "coretemp") != 0) continue;

		for (int i = 1; i <= 8; i++) {
			char lp[320], lab[64] = "";
			snprintf(lp, sizeof lp, "/sys/class/hwmon/%s/temp%d_label", e->d_name, i);
			FILE *lf = fopen(lp, "r"); if (!lf) continue;
			if (fgets(lab, sizeof lab, lf)) lab[strcspn(lab, "\n")] = 0;
			fclose(lf);
			if (strncmp(lab, "Package", 7) == 0) {
				snprintf(cpu_temp_path, sizeof cpu_temp_path,
				         "/sys/class/hwmon/%s/temp%d_input", e->d_name, i);
				break;
			}
		}
		if (!cpu_temp_path[0])
			snprintf(cpu_temp_path, sizeof cpu_temp_path,
			         "/sys/class/hwmon/%s/temp1_input", e->d_name);
		break;
	}
	closedir(d);
}

static void sample_cpu_temp(void) {
	detect_cpu_temp();
	if (!cpu_temp_path[0]) return;
	int milli = read_int_file(cpu_temp_path);
	if (milli < 0) return;
	cached_cpu_temp = (milli + 500) / 1000;
}

static void sample_disk(void) {
	struct statvfs s;
	if (statvfs("/", &s) || s.f_blocks == 0) return;
	cached_disk = (int)(((s.f_blocks - s.f_bavail) * 100) / s.f_blocks);
}

static void detect_bat(void) {
	if (bat_dev[0]) return;
	DIR *d = opendir("/sys/class/power_supply"); if (!d) return;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (strncmp(e->d_name, "BAT", 3) == 0) {
			snprintf(bat_dev, sizeof bat_dev, "%s", e->d_name);
			break;
		}
	}
	closedir(d);
}

static void sample_bat(void) {
	detect_bat();
	if (!bat_dev[0]) { cached_bat_pct = -1; return; }
	char path[384];
	snprintf(path, sizeof path, "/sys/class/power_supply/%s/capacity", bat_dev);
	int pct = read_int_file(path);
	if (pct < 0) { cached_bat_pct = -1; return; }
	cached_bat_pct = pct;
	snprintf(path, sizeof path, "/sys/class/power_supply/%s/status", bat_dev);
	FILE *f = fopen(path, "r");
	cached_bat_chg = 0;
	if (f) {
		char st[32] = "";
		if (fgets(st, sizeof st, f))
			cached_bat_chg = (strstr(st, "Charging") || strstr(st, "Full")) ? 1 : 0;
		fclose(f);
	}
}

static void detect_bri(void) {
	if (bri_dev[0]) return;
	DIR *d = opendir("/sys/class/backlight"); if (!d) return;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] != '.') {
			snprintf(bri_dev, sizeof bri_dev, "%s", e->d_name);
			break;
		}
	}
	closedir(d);
}

static void sample_bri(void) {
	detect_bri();
	if (!bri_dev[0]) return;
	char path[384];
	snprintf(path, sizeof path, "/sys/class/backlight/%s/actual_brightness", bri_dev);
	int b = read_int_file(path);
	snprintf(path, sizeof path, "/sys/class/backlight/%s/max_brightness", bri_dev);
	int m = read_int_file(path);
	if (b < 0 || m <= 0) return;
	int pct = b * 100 / m;
	cached_bri_idx = pct >= 88 ? 6 : pct >= 75 ? 5 : pct >= 60 ? 4
	              : pct >= 45 ? 3 : pct >= 30 ? 2 : pct >= 15 ? 1 : 0;
}

static void sample_wifi(void) {
	FILE *f = fopen("/proc/net/wireless", "r");
	if (!f) { cached_wifi_icon = I_WIFI_OFF; return; }
	char buf[256]; int line = 0;
	cached_wifi_icon = I_WIFI_OFF;
	while (fgets(buf, sizeof buf, f)) {
		if (++line < 3) continue;
		char *p = strchr(buf, ':'); if (!p) continue;
		int status, link;
		if (sscanf(p+1, " %d %d", &status, &link) != 2) continue;
		(void)status;
		if (link <= 0)        cached_wifi_icon = I_WIFI_OFF;
		else if (link >= 55)  cached_wifi_icon = I_WIFI[3];
		else if (link >= 40)  cached_wifi_icon = I_WIFI[2];
		else if (link >= 25)  cached_wifi_icon = I_WIFI[1];
		else                  cached_wifi_icon = I_WIFI[0];
		break;
	}
	fclose(f);
}

/* ---------- render (pure cache → stdout) ---------- */
static const char *bat_glyph(int pct, int chg) {
	if (chg) return I_BAT_CHG;
	if (pct > 87) return I_BAT_FULL;
	if (pct > 62) return I_BAT_75;
	if (pct > 37) return I_BAT_50;
	if (pct > 12) return I_BAT_25;
	return I_BAT_EMPTY;
}

static void render(void) {
	const char *vi = (cached_muted || cached_vol < 0) ? I_VOL_OFF
	               : cached_vol <= 33 ? I_VOL_LO : I_VOL_HI;
	char bat[64];
	if (cached_bat_pct < 0) bat[0] = 0;
	else snprintf(bat, sizeof bat, "%s %d%%",
	              bat_glyph(cached_bat_pct, cached_bat_chg), cached_bat_pct);

	char cpu[64];
	if (cached_cpu_temp < 0)
		snprintf(cpu, sizeof cpu, "%s %d.%d%%", I_CPU, cached_cpu_t / 10, cached_cpu_t % 10);
	else
		snprintf(cpu, sizeof cpu, "%s %d.%d%%   %s %d\xc2\xb0""C",
		         I_CPU, cached_cpu_t / 10, cached_cpu_t % 10,
		         I_TEMP, cached_cpu_temp);

	printf("^fg(" FG ")  %s %d%%   %s   %s    "
	       "^fg(" SEP ")/    "
	       "^fg(" FG ")%s   %s   %s  \n",
	       I_DISK, cached_disk,
	       cpu,
	       bat, vi, I_BRI[cached_bri_idx], cached_wifi_icon);
	fflush(stdout);
}

/* ---------- pulse: update cache only, never render ---------- */
static pa_mainloop_api *pa_api_g;
static pa_context      *pa_ctx;
static char default_sink[256] = "";

static void context_state_cb(pa_context *c, void *u);

static void sink_info_cb(pa_context *c, const pa_sink_info *i, int eol, void *u) {
	(void)c; (void)u;
	if (eol || !i) return;
	pa_volume_t v = pa_cvolume_avg(&i->volume);
	cached_vol   = (int)((100.0 * v / PA_VOLUME_NORM) + 0.5);
	cached_muted = i->mute ? 1 : 0;
}

static void server_info_cb(pa_context *c, const pa_server_info *si, void *u) {
	(void)u;
	if (!si || !si->default_sink_name) return;
	snprintf(default_sink, sizeof default_sink, "%s", si->default_sink_name);
	pa_operation *o = pa_context_get_sink_info_by_name(c, default_sink, sink_info_cb, NULL);
	if (o) pa_operation_unref(o);
}

static void subscribe_cb(pa_context *c, pa_subscription_event_type_t t,
                         uint32_t idx, void *u) {
	(void)idx; (void)u;
	pa_subscription_event_type_t fac = t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
	if (fac == PA_SUBSCRIPTION_EVENT_SERVER) {
		pa_operation *o = pa_context_get_server_info(c, server_info_cb, NULL);
		if (o) pa_operation_unref(o);
	} else if (fac == PA_SUBSCRIPTION_EVENT_SINK && default_sink[0]) {
		pa_operation *o = pa_context_get_sink_info_by_name(c, default_sink, sink_info_cb, NULL);
		if (o) pa_operation_unref(o);
	}
}

static void pa_reconnect(pa_mainloop_api *a, pa_time_event *e,
                         const struct timeval *tv, void *u) {
	(void)tv; (void)u;
	a->time_free(e);
	if (pa_ctx) { pa_context_disconnect(pa_ctx); pa_context_unref(pa_ctx); }
	default_sink[0] = '\0';
	pa_ctx = pa_context_new(a, "dwlb-status");
	pa_context_set_state_callback(pa_ctx, context_state_cb, NULL);
	pa_context_connect(pa_ctx, NULL, PA_CONTEXT_NOFLAGS, NULL);
}

static void context_state_cb(pa_context *c, void *u) {
	(void)u;
	switch (pa_context_get_state(c)) {
	case PA_CONTEXT_READY: {
		pa_context_set_subscribe_callback(c, subscribe_cb, NULL);
		pa_operation *s = pa_context_subscribe(c,
			PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER, NULL, NULL);
		if (s) pa_operation_unref(s);
		pa_operation *o = pa_context_get_server_info(c, server_info_cb, NULL);
		if (o) pa_operation_unref(o);
		break;
	}
	case PA_CONTEXT_FAILED:
	case PA_CONTEXT_TERMINATED: {
		cached_vol = -1; cached_muted = 0;
		struct timeval tv; gettimeofday(&tv, NULL); tv.tv_sec += 3;
		pa_api_g->time_new(pa_api_g, &tv, pa_reconnect, NULL);
		break;
	}
	default: break;
	}
}

/* ---------- timerfd tick (sole render driver) ---------- */
static unsigned tick_n = 0;

static void on_tick(pa_mainloop_api *a, pa_io_event *e, int fd,
                    pa_io_event_flags_t f, void *u) {
	(void)a; (void)e; (void)f; (void)u;
	uint64_t exp;
	if (read(fd, &exp, sizeof exp) != sizeof exp) return;
	sample_cpu();
	sample_cpu_temp();
	sample_bri();
	if (tick_n %   5 == 0) sample_wifi();
	if (tick_n %  30 == 0) sample_bat();
	if (tick_n % 300 == 0) sample_disk();
	render();
	tick_n++;
}

/* ---------- signals ---------- */
static void on_signal(pa_mainloop_api *a, pa_io_event *e, int fd,
                      pa_io_event_flags_t f, void *u) {
	(void)e; (void)f; (void)u;
	struct signalfd_siginfo si;
	while (read(fd, &si, sizeof si) == (ssize_t)sizeof si) {
		if (si.ssi_signo == SIGTERM || si.ssi_signo == SIGINT) {
			a->quit(a, 0);
			return;
		}
		if (si.ssi_signo == SIGUSR1) {
			sample_cpu(); sample_cpu_temp(); sample_bri(); sample_wifi();
			sample_bat(); sample_disk();
			render();
		}
	}
}

/* ---------- main ---------- */
int main(void) {
	signal(SIGPIPE, SIG_IGN);

	pa_mainloop *pa_ml = pa_mainloop_new();
	if (!pa_ml) return 1;
	pa_api_g = pa_mainloop_get_api(pa_ml);

	pa_ctx = pa_context_new(pa_api_g, "dwlb-status");
	pa_context_set_state_callback(pa_ctx, context_state_cb, NULL);
	pa_context_connect(pa_ctx, NULL, PA_CONTEXT_NOFLAGS, NULL);

	int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	if (tfd < 0) return 1;
	struct itimerspec ts = {
		.it_value    = { .tv_sec = 1, .tv_nsec = 0 },
		.it_interval = { .tv_sec = 1, .tv_nsec = 0 },
	};
	timerfd_settime(tfd, 0, &ts, NULL);
	pa_api_g->io_new(pa_api_g, tfd, PA_IO_EVENT_INPUT, on_tick, NULL);

	sigset_t mask; sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1); sigaddset(&mask, SIGTERM); sigaddset(&mask, SIGINT);
	sigprocmask(SIG_BLOCK, &mask, NULL);
	int sfd = signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK);
	if (sfd >= 0)
		pa_api_g->io_new(pa_api_g, sfd, PA_IO_EVENT_INPUT, on_signal, NULL);

	/* Prime cache so the first paint isn't all zeros */
	sample_cpu(); sample_cpu_temp(); sample_bri(); sample_wifi();
	sample_bat(); sample_disk();
	render();

	int rv = 0;
	pa_mainloop_run(pa_ml, &rv);

	if (pa_ctx) { pa_context_disconnect(pa_ctx); pa_context_unref(pa_ctx); }
	pa_mainloop_free(pa_ml);
	return rv;
}
