/* twl — user-facing knobs. Self-contained (does NOT include dwlarp config). */
#ifndef TWL_CONFIG_H
#define TWL_CONFIG_H

/* ---------- Bar ---------- */
#define BAR_HEIGHT      28
#define BAR_PAD_X       16   /* L/R inset (was 10 — felt cramped against logo) */
#define BAR_TAG_PAD_X   10   /* horizontal padding inside each tag cell (dwlb-ish) */
#define BAR_TAG_GAP      0   /* dwlb runs tags edge-to-edge */

#define BAR_BG          0xcc000000u  /* ARGB; alpha respected by compositor */
#define BAR_FG          0xffffffffu
#define BAR_DIM         0xff7a808bu  /* separators */
#define BAR_ACTIVE_BG   0xcc2a2f3au  /* active workspace tag */
#define BAR_URGENT_BG   0xffee3300u

/* VPN colors (match dwlarp WS_STATUS_VPN_*_FG) */
#define VPN_ON_FG       0xff7fbf9fu
#define VPN_STALE_FG    0xffff5050u
#define VPN_OFF_FG      0xffffaa20u

/* ---------- HUD button panel (ws-hud clone) ---------- */
#define HUD_BG          0xcc000000u
#define HUD_FG          0xff1e3a3au   /* border, =WS_BORDER */
#define HUD_ON          0xff3a7268u   /* toggle on, =WS_FOCUS */
#define HUD_HOLD        0xff5fa090u
#define HUD_WARN        0xffd04848u
#define HUD_ICON        0xffffffffu

#define HUD_BTN_W       44
#define HUD_BTN_H       44
#define HUD_BTN_GAP     8
#define HUD_BTN_BORDER  2
#define HUD_PAD         8
#define HUD_OUTER       2
#define HUD_BAR_OVERLAP 16   /* px buttons protrude up into bar */
#define HUD_TRIG_H      5    /* hover-trigger strip */

#define HUD_HIDE_DELAY_MS  30
#define HUD_CLICK_GRACE_MS 100
#define HUD_ANIM_TAU_MS    50.0
#define HUD_ANIM_FRAME_MS  16
#define HUD_ANIM_EPSILON   0.5
#define HUD_STARTUP_GRACE_MS 800

/* HUD buttons. { type, on_cmd, off_cmd, warn_cmd, state_cmd, icon }
 *   type: 0=click, 1=toggle
 *   warn_cmd NULL => falls back to on_cmd
 *   state_cmd NULL => stays off */
#define HUD_BUTTONS_INIT { \
    { 1, "twlctl gamma flat", "twlctl gamma off", NULL, "twlctl gamma is-warm", 0xf186 }, \
    { 1, "twlctl dnd on", "twlctl dnd off", NULL, "twlctl dnd status", 0xf1f6 }, \
    { 1, "ws-hud-lidlock on", "ws-hud-lidlock off", NULL, "ws-hud-lidlock status | grep -qx on", 0xf023 }, \
    { 1, "foot -T ws-hud-mullvad --app-id=ws-hud-mullvad -e mullvad-menu", \
         "sudo -n mullvad-vpn down", \
         "sudo -n mullvad-vpn reconnect", "mullvad-vpn health", 0xf132 }, \
    { 0, "foot -T ws-hud-bt   --app-id=ws-hud-bt   -e bluetuith --no-warning", NULL, NULL, NULL, 0xf293 }, \
    { 0, "foot -T ws-hud-wifi --app-id=ws-hud-wifi -e impala",                 NULL, NULL, NULL, 0xf1eb }, \
    { 0, "foot -T ws-hud-vol  --app-id=ws-hud-vol  -e pulsemixer",             NULL, NULL, NULL, 0xf028 }, \
}

/* ---------- Menu (dmenu-wl clone, horizontal top bar) ---------- */
#define MENU_BG         0xff000000u   /* opaque black, matches dmenu-wl -nb */
#define MENU_FG         0xffffffffu   /* dmenu-wl -nf */
#define MENU_SEL_BG     0xff2a2f3au   /* dmenu-wl -sb */
#define MENU_SEL_FG     0xffffffffu   /* dmenu-wl -sf */
#define MENU_DIM        0xff7a808bu

#define MENU_HEIGHT     28            /* dmenu-wl -h 28 */
#define MENU_PAD_X      8             /* L/R inset */
#define MENU_ITEM_PAD_X 8             /* horizontal padding inside each item slab */
#define MENU_GAP        12            /* prompt → first item */
#define MENU_PROMPT     "run:"

/* ---------- Status sampling ---------- */
#define VPN_STALE_S        180
#define STATUS_CADENCE_WIFI   5
#define STATUS_CADENCE_BAT   30
#define STATUS_CADENCE_DISK 300

/* Low-battery notification thresholds. Fires once when bat_pct crosses each
 * threshold downward while discharging; charging back above clears the latch
 * so the next discharge will fire again. */
#define BAT_WARN_PCT  15
#define BAT_CRIT_PCT   5

#define TIME_FMT "%H:%M"
#define DATE_FMT "%b %d"

/* Tag labels (9). */
#define TAG_LABELS { "1","2","3","4","5","6","7","8","9" }

/* Show modules */
#define SHOW_DISK    1
#define SHOW_CPU     1
#define SHOW_MEM     1
#define SHOW_BAT     1
#define SHOW_VPN     1
#define SHOW_WIFI    1

#define CTL_SOCK_NAME "twl.sock"

/* ---------- Powermenu (built-in `twlctl powermenu`) ----------
 * Each entry: { icon-codepoint, label, shell-command }. Icons must also
 * appear in tools/bake.c's ICONS[]. */
#define POWERMENU_INIT { \
    { 0xf011, "Poweroff",  "loginctl poweroff"  }, \
    { 0xf021, "Reboot",    "loginctl reboot"    }, \
    { 0xf28d, "Hibernate", "loginctl hibernate" }, \
    { 0xf08b, "Logout",    "pkill -x dwl"       }, \
}

/* ---------- Wallpaper ----------
 * PNG decoded once at first configure. If the file is missing or the decoder
 * rejects it, the background falls back to WALL_BG (solid). */
#define WALL_PATH "~/.local/share/dwl/wallpaper.png"
#define WALL_BG   0xff101418u

/* ---------- OSD / notifications (mako + dwl-osd replacement) ----------
 * Top-center stack. Each slab fixed-size, rendered into one tall surface;
 * inactive slots are transparent so the unused area is click-through.
 * Colors transcribed from the prior mako config to keep a consistent look. */
#define OSD_W              340
#define OSD_SLAB_H          60
#define OSD_GAP              8
#define OSD_TOP_MARGIN     (BAR_HEIGHT + 8)
#define OSD_PROG_H          10   /* thicker, easier to see at a glance */
#define OSD_PROG_TRACK_BG   0xff1a3630u  /* dim trough behind the fill */
#define OSD_PAD_X           14
#define OSD_ICON_GAP        12

#define OSD_BG          0xe60d1418u
#define OSD_FG          0xffc8e8f0u
#define OSD_BORDER      0xff3a7268u
#define OSD_BORDER_CRIT 0xffffffffu
#define OSD_BORDER_MUTE 0xffa04050u
#define OSD_BG_MUTE     0xe63a1418u
#define OSD_FG_MUTE     0xfff0c8c8u
#define OSD_BORDER_WARN 0xffe0c060u   /* low-battery warn slab (yellow) */
#define OSD_BG_WARN     0xe6332a14u
#define OSD_FG_WARN     0xfff0e0a8u
#define OSD_PROG_FG     0xff5fa090u   /* brighter teal — was 0xff1a3630 */
#define OSD_PROG_FG_MUTE 0xffd06070u
#define OSD_PROG_FG_WARN 0xffe0c060u

/* Default timeouts (ms). Critical sticky (caller passes 0). */
#define OSD_TIMEOUT_LOW       3000
#define OSD_TIMEOUT_NORMAL    5000
#define OSD_TIMEOUT_OSD       1200

/* ---------- Gamma / night mode (wlsunset replacement) ----------
 * Hard-step at NIGHT_HOUR (warm) and DAY_HOUR (cool). FLAT_K is the
 * HUD-button override (manual warm; intentionally warmer than NIGHT_K
 * so the toggle is visually distinct from scheduled night). */
#define GAMMA_DAY_K     6500
#define GAMMA_NIGHT_K   2800
#define GAMMA_FLAT_K    2400
#define GAMMA_DAY_HOUR  7
#define GAMMA_NIGHT_HOUR 20

/* ---------- Session lock (swaylock replacement) ----------
 * PAM service to authenticate against. "system-auth" is the conventional
 * shared stack used by login/sudo/su. "swaylock" works too if it exists. */
#define LOCK_PAM_SERVICE "system-auth"
#define LOCK_HELPER_BIN  "twl-lock-helper"
#define LOCK_BG          0xff000000u
#define LOCK_RING        0xff3a7268u
#define LOCK_RING_WRONG  0xffd06878u
#define LOCK_FG          0xffa8d5ccu
#define LOCK_DIM         0xff7a808bu
#define LOCK_CAPS        0xffe0c060u   /* caps-lock indicator below the dots */
#define LOCK_PROMPT      "Password:"
#define LOCK_WRONG_MS    1200    /* show wrong-state ring this long after failed try */

#endif
