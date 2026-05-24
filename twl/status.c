/* System metric sampling (mirrors dwlb-status semantics, no libpulse). */
#include "twl.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

Status status;

static char bat_dev[256];
static char cpu_temp_path[320];
static long long prev_busy, prev_total;

/* Low-battery edge-trigger latches: bit set = a fired notification has not yet
 * been cleared by charging/recovery. Prevents per-sample spam while keeping
 * the alert sticky enough to notice. */
static int bat_warn_fired;
static int bat_crit_fired;
/* Persistent notification ids so subsequent warn/crit posts replace in place
 * instead of stacking new slabs every battery sample. */
#define BAT_OSD_ID 0xb47b47fu

static int read_int_file(const char *p) {
    FILE *f = fopen(p, "r"); if (!f) return -1;
    int v = -1; if (fscanf(f, "%d", &v) != 1) v = -1;
    fclose(f); return v;
}

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
    status.cpu_t10 = dt > 0 ? (int)(db * 1000 / dt) : 0;
}

static void detect_cpu_temp(void) {
    if (cpu_temp_path[0]) return;
    DIR *d = opendir("/sys/class/hwmon"); if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char np[320], name[64] = "";
        snprintf(np, sizeof np, "/sys/class/hwmon/%s/name", e->d_name);
        FILE *f = fopen(np, "r"); if (!f) continue;
        if (!fgets(name, sizeof name, f)) { fclose(f); continue; }
        fclose(f);
        name[strcspn(name, "\n")] = 0;
        if (strcmp(name, "coretemp") != 0 && strcmp(name, "k10temp") != 0) continue;
        for (int i = 1; i <= 8; i++) {
            char lp[320], lab[64] = "";
            snprintf(lp, sizeof lp, "/sys/class/hwmon/%s/temp%d_label", e->d_name, i);
            FILE *lf = fopen(lp, "r"); if (!lf) continue;
            if (fgets(lab, sizeof lab, lf)) lab[strcspn(lab, "\n")] = 0;
            fclose(lf);
            if (strncmp(lab, "Package", 7) == 0 || strncmp(lab, "Tctl", 4) == 0) {
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
    status.cpu_temp = (milli + 500) / 1000;
}

static void sample_mem(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) { status.mem_used_kb = -1; return; }
    long total = -1, avail = -1;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "MemTotal:", 9))         sscanf(line + 9,  "%ld", &total);
        else if (!strncmp(line, "MemAvailable:", 13)) sscanf(line + 13, "%ld", &avail);
        if (total >= 0 && avail >= 0) break;
    }
    fclose(f);
    if (total < 0 || avail < 0) { status.mem_used_kb = -1; return; }
    long used = total - avail;
    status.mem_used_kb = used > 0 ? (int)used : 0;
}

static void sample_disk(void) {
    struct statvfs s;
    if (statvfs("/", &s) || s.f_blocks == 0) return;
    unsigned long long total = (unsigned long long)s.f_blocks * s.f_frsize;
    unsigned long long used  = (unsigned long long)(s.f_blocks - s.f_bfree) * s.f_frsize;
    FILE *sw = fopen("/proc/swaps", "re");
    if (sw) {
        char line[512];
        (void)!fgets(line, sizeof line, sw);
        while (fgets(line, sizeof line, sw)) {
            char path[256], type[32];
            unsigned long long size_kb;
            if (sscanf(line, "%255s %31s %llu", path, type, &size_kb) == 3
                && strcmp(type, "file") == 0) {
                unsigned long long b = size_kb * 1024ULL;
                if (b < used) used -= b; else used = 0;
            }
        }
        fclose(sw);
    }
    status.disk_pct = total ? (int)((used * 100) / total) : 0;
}

static void detect_bat(void) {
    if (bat_dev[0]) return;
    DIR *d = opendir("/sys/class/power_supply"); if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "BAT", 3) == 0) {
            snprintf(bat_dev, sizeof bat_dev, "%s", e->d_name); break;
        }
    }
    closedir(d);
}

static void sample_bat(void) {
    detect_bat();
    if (!bat_dev[0]) { status.bat_pct = -1; return; }
    char path[384];
    snprintf(path, sizeof path, "/sys/class/power_supply/%s/capacity", bat_dev);
    int pct = read_int_file(path);
    if (pct < 0) { status.bat_pct = -1; return; }
    status.bat_pct = pct;
    snprintf(path, sizeof path, "/sys/class/power_supply/%s/status", bat_dev);
    FILE *f = fopen(path, "r");
    status.bat_charging = 0;
    if (f) {
        char st[32] = "";
        if (fgets(st, sizeof st, f))
            status.bat_charging = (strstr(st, "Charging") || strstr(st, "Full")) ? 1 : 0;
        fclose(f);
    }

    /* Charging or recovered above the warn threshold → reset latches so the
     * next discharge cycle re-arms the alert. */
    if (status.bat_charging || pct > BAT_WARN_PCT) {
        if (bat_warn_fired || bat_crit_fired) osd_close(BAT_OSD_ID);
        bat_warn_fired = bat_crit_fired = 0;
        return;
    }

    char body[64];
    if (pct <= BAT_CRIT_PCT && !bat_crit_fired) {
        snprintf(body, sizeof body, "%d%% remaining — plug in now", pct);
        /* style=1 (red), urgency=2 (critical, sticky), no timeout. */
        osd_post(BAT_OSD_ID, "Battery critical", body, 0xf244, -1, 2, 1, 0);
        bat_crit_fired = bat_warn_fired = 1;
    } else if (pct <= BAT_WARN_PCT && !bat_warn_fired) {
        snprintf(body, sizeof body, "%d%% remaining", pct);
        /* style=2 (yellow), urgency=1 (normal). */
        osd_post(BAT_OSD_ID, "Battery low", body, 0xf243, -1, 1, 2, 0);
        bat_warn_fired = 1;
    }
}

static void sample_vpn(void) {
    FILE *f = fopen("/run/mullvad.handshake", "r");
    if (!f) {
        status.vpn_state = access("/sys/class/net/mullvad", 0) == 0 ? 1 : 0;
        return;
    }
    long long hs = 0;
    if (fscanf(f, "%lld", &hs) != 1) hs = 0;
    fclose(f);
    if (hs <= 0) { status.vpn_state = 0; return; }
    long long now = (long long)time(NULL);
    status.vpn_state = (now - hs <= VPN_STALE_S) ? 1 : 2;
}

static void sample_wifi(void) {
    FILE *f = fopen("/proc/net/wireless", "r");
    if (!f) { status.wifi_level = -1; return; }
    char buf[256]; int line = 0;
    status.wifi_level = -1;
    while (fgets(buf, sizeof buf, f)) {
        if (++line < 3) continue;
        char *p = strchr(buf, ':'); if (!p) continue;
        int st_, link;
        if (sscanf(p+1, " %d %d", &st_, &link) != 2) continue;
        (void)st_;
        if (link <= 0)        status.wifi_level = -1;
        else if (link >= 55)  status.wifi_level = 3;
        else if (link >= 40)  status.wifi_level = 2;
        else if (link >= 25)  status.wifi_level = 1;
        else                  status.wifi_level = 0;
        break;
    }
    fclose(f);
}

void status_init(void) {
    status.cpu_temp = -1;
    status.bat_pct  = -1;
    status.mem_used_kb = -1;
    status.wifi_level = -1;
}

void status_sample_all(void) {
    sample_cpu(); sample_cpu_temp(); sample_mem(); sample_disk();
    sample_bat(); sample_vpn(); sample_wifi();
}

void status_tick(int tick_n) {
    sample_cpu(); sample_cpu_temp(); sample_mem(); sample_vpn();
    if (tick_n % STATUS_CADENCE_WIFI == 0) sample_wifi();
    if (tick_n % STATUS_CADENCE_BAT  == 0) sample_bat();
    if (tick_n % STATUS_CADENCE_DISK == 0) sample_disk();
}
