/* media.c — twlctl volume / mic / backlight. Replaces dwl-osd shell.
 *
 *   volume up|down|mute → wpctl @DEFAULT_AUDIO_SINK@   + OSD slot 1
 *   mic mute            → wpctl @DEFAULT_AUDIO_SOURCE@ + OSD slot 2
 *   backlight up|down   → /sys/class/backlight/<dev>/brightness + OSD slot 3
 *
 * wpctl is part of wireplumber (already mandatory). Backlight is a direct
 * sysfs write; brightnessctl ships a udev rule that makes that file group-
 * writable by `video`, which dwlarp users already need to be in. */

#include "twl.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SLOT_VOL 1
#define SLOT_MIC 2
#define SLOT_BRI 3

#define ICON_VOL    0xf028
#define ICON_VOL_X  0xf026
#define ICON_MIC    0xf130
#define ICON_MIC_X  0xf131
#define ICON_SUN    0xf185

#define VOL_STEP 5
#define VOL_CAP  150
#define BRI_STEP 5

static int popen_read(const char *cmd, char *out, int cap) {
    FILE *f = popen(cmd, "r");
    if (!f) return -1;
    int n = (int)fread(out, 1, cap - 1, f);
    out[n > 0 ? n : 0] = 0;
    return pclose(f);
}

/* Parses `wpctl get-volume <target>` output:
 *   "Volume: 0.42 [MUTED]\n" */
static int wp_get(const char *target, int *pct, int *muted) {
    char cmd[160]; snprintf(cmd, sizeof cmd, "wpctl get-volume %s 2>/dev/null", target);
    char out[160]; if (popen_read(cmd, out, sizeof out) < 0) return -1;
    float v = 0;
    if (sscanf(out, "Volume: %f", &v) != 1) return -1;
    *pct   = (int)(v * 100 + 0.5f);
    *muted = strstr(out, "MUTED") != NULL;
    return 0;
}
static void wp_set_vol(const char *target, int pct) {
    char cmd[160]; snprintf(cmd, sizeof cmd, "wpctl set-volume %s %d%% 2>/dev/null", target, pct);
    int rc = system(cmd); (void)rc;
}
static void wp_set_mute(const char *target, const char *arg) {
    char cmd[160]; snprintf(cmd, sizeof cmd, "wpctl set-mute %s %s 2>/dev/null", target, arg);
    int rc = system(cmd); (void)rc;
}

void media_volume(const char *arg) {
    const char *T = "@DEFAULT_AUDIO_SINK@";
    int pct = 0, muted = 0;
    if (wp_get(T, &pct, &muted) < 0) return;

    if (!strcmp(arg, "mute")) {
        wp_set_mute(T, "toggle");
        muted = !muted;
    } else {
        int dir = !strcmp(arg, "up") ? 1 : !strcmp(arg, "down") ? -1 : 0;
        if (!dir) return;
        int n = pct + dir * VOL_STEP;
        if (n < 0) n = 0;
        if (n > VOL_CAP) n = VOL_CAP;
        if (muted) { wp_set_mute(T, "0"); muted = 0; }
        wp_set_vol(T, n);
        pct = n;
    }
    osd_post(SLOT_VOL, muted ? "Volume muted" : "Volume", "",
             muted ? ICON_VOL_X : ICON_VOL, pct, 1, muted, OSD_TIMEOUT_OSD);
}

void media_mic(const char *arg) {
    const char *T = "@DEFAULT_AUDIO_SOURCE@";
    if (strcmp(arg, "mute") != 0) return;
    int pct = 0, muted = 0;
    wp_get(T, &pct, &muted);
    wp_set_mute(T, "toggle");
    muted = !muted;
    osd_post(SLOT_MIC, muted ? "Microphone muted" : "Microphone", "",
             muted ? ICON_MIC_X : ICON_MIC, -1, 1, muted, OSD_TIMEOUT_OSD);
}

/* Cache the first /sys/class/backlight/<dev> path. Single-display assumption
 * matches twl's single-output binding elsewhere. */
static const char *bl_dev(void) {
    static char dev[320];  /* /sys/class/backlight/ (21) + NAME_MAX (255) + slack */
    if (dev[0]) return dev;
    DIR *d = opendir("/sys/class/backlight");
    if (!d) return NULL;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        snprintf(dev, sizeof dev, "/sys/class/backlight/%s", e->d_name);
        break;
    }
    closedir(d);
    return dev[0] ? dev : NULL;
}

static int read_int_file(const char *path) {
    int fd = open(path, O_RDONLY); if (fd < 0) return -1;
    char b[32]; int n = (int)read(fd, b, sizeof b - 1); close(fd);
    if (n <= 0) return -1;
    b[n] = 0; return atoi(b);
}
static int write_int_file(const char *path, int v) {
    int fd = open(path, O_WRONLY); if (fd < 0) return -1;
    char b[32]; int n = snprintf(b, sizeof b, "%d", v);
    int r = (int)write(fd, b, n); close(fd);
    return r < 0 ? -1 : 0;
}

void media_backlight(const char *arg) {
    int dir = !strcmp(arg, "up") ? 1 : !strcmp(arg, "down") ? -1 : 0;
    if (!dir) return;
    const char *dev = bl_dev(); if (!dev) return;
    char cur_p[360], max_p[360];
    snprintf(cur_p, sizeof cur_p, "%s/brightness", dev);
    snprintf(max_p, sizeof max_p, "%s/max_brightness", dev);
    int cur = read_int_file(cur_p), max = read_int_file(max_p);
    if (cur < 0 || max <= 0) return;
    int pct = cur * 100 / max;
    int n;
    if (dir > 0) {
        if (pct >= 100) return;
        n = pct < 5 ? pct + 1 : pct + BRI_STEP;
        if (n > 100) n = 100;
    } else {
        if (pct <= 1) return;
        n = pct <= 5 ? pct - 1 : pct - BRI_STEP;
    }
    write_int_file(cur_p, n * max / 100);
    osd_post(SLOT_BRI, "Brightness", "", ICON_SUN, n, 1, 0, OSD_TIMEOUT_OSD);
}
