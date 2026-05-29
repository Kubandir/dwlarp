/* ============================================================
 * dwlarp — single user-facing config.
 *
 * Edit values here, then re-run ./install.sh.  Everything
 * downstream (compositor, bar, status, lock screen) is driven
 * by these macros.  No other file should need touching.
 * ============================================================ */

#ifndef DWLARP_CONFIG_H
#define DWLARP_CONFIG_H

/* Per-host knob read by install.sh to gate laptop-only installation:
 * mullvad scripts/services, elogind resume hooks, lid-lock helper.
 * Display-side knobs (WS_STATUS_BATTERY, WS_STATUS_WIFI, etc.) still
 * decide what renders in the bar regardless of this. */
#define WS_FORM_FACTOR     "laptop"

/* GPU vendor — installer reads this to wire renderer + VA-API + cursor env
 * into ~/.config/dwlarp/env. "intel" leaves wlroots on its default (gles2 +
 * HW cursors, fine on Intel/AMD). "nvidia" picks the vulkan renderer + SW
 * cursor + nvidia-vaapi-driver. */
#define WS_GPU             "intel"

/* ------------------------------------------------------------
 * COLORS  (0xRRGGBBAA — alpha for transparency where supported)
 * ------------------------------------------------------------ */
#define WS_BG              0x222222ff   /* desktop root color */
#define WS_BORDER          0x1e3a3aff   /* unfocused window border */
#define WS_FOCUS           0x3a7268ff   /* focused window border + accents */
#define WS_URGENT          0xff0000ff   /* urgent / wrong-input flash */

#define WS_BAR_BG          0x000000cc   /* bar background */
#define WS_BAR_FG          0xffffffff   /* bar foreground (all text) */
#define WS_BAR_ACTIVE_BG   0x2a2f3acc   /* active workspace tag background */
#define WS_BAR_URGENT_BG   0xee3300ff   /* urgent tag background */

/* swaylock colors — hex strings, no 0x prefix, alpha optional */
#define WS_LOCK_SCREEN_HEX "000000"
#define WS_LOCK_RING_HEX   "3a7268a6"
#define WS_LOCK_TEXT_HEX   "a8d5cc"
#define WS_LOCK_WRONG_HEX  "d06878a6"

/* Status-bar text colors — ^fg(...) markup understood by dwlb.
 * Right-side (dwlb-status): main text + dim separator.
 * Left-side  (dwlb-leftstatus): logo/clock + date. */
#define WS_STATUS_FG       "#ffffff"
#define WS_STATUS_SEP      "#7a808b"
#define WS_LEFTST_FG       "#ffffff"
#define WS_LEFTST_DATE_FG  "#cfd3da"

/* VPN shield colors in the bar. The shield is the most safety-critical
 * indicator on the bar — STALE means traffic is black-holing in a dead
 * tunnel, OFF means the killswitch is the only thing stopping a leak.
 * Both are loud on purpose. ON is muted teal so a working tunnel doesn't
 * pull the eye. */
#define WS_STATUS_VPN_ON_FG     "#7fbf9f"   /* healthy — handshake fresh */
#define WS_STATUS_VPN_STALE_FG  "#ff5050"   /* iface up, no recent handshake */
#define WS_STATUS_VPN_OFF_FG    "#ffaa20"   /* tunnel down — killswitch active */

/* ------------------------------------------------------------
 * FONTS
 * ------------------------------------------------------------ */
#define WS_BAR_FONT        "FiraCode Nerd Font:weight=bold:size=11, Symbols Nerd Font:size=12"
#define WS_LOCK_FONT       "FiraCode Nerd Font"
#define WS_LOCK_FONT_SIZE  14

/* ------------------------------------------------------------
 * WALLPAPER  (path relative to repo root)
 * ------------------------------------------------------------ */
#define WS_WALLPAPER       "assets/wallpaper.png"

/* ------------------------------------------------------------
 * COMPOSITOR — window manager behaviour
 * ------------------------------------------------------------ */
#define WS_SLOPPY_FOCUS    1       /* 1 = focus follows mouse */
#define WS_BORDER_PX       2       /* window border thickness (pixels) */
#define WS_BORDER_RADIUS   10      /* window corner radius (px, 0 = square) */
#define WS_SHADOW_SIGMA    10.0f   /* shadow blur radius (px) */
#define WS_SHADOW_ALPHA    0.40f   /* shadow opacity (0.0–1.0) */
#define WS_GAP_PX          6       /* gap between tiled windows (pixels) */
#define WS_MFACT           0.55f   /* default master/stack split [0.05..0.95] */

/* Monitor mode override. WS_MONITOR_NAME is a substring matched against
 * wlroots output names; NULL matches every output. WS_MONITOR_REFRESH_MHZ=0
 * keeps wlroots' preferred mode (what we want on the laptop). See the
 * tower config for the non-zero case. */
#define WS_MONITOR_NAME        NULL
#define WS_MONITOR_WIDTH       0
#define WS_MONITOR_HEIGHT      0
#define WS_MONITOR_REFRESH_MHZ 0

/* ------------------------------------------------------------
 * KEYBOARD
 *
 * WS_KBD_LAYOUT — XKB layout. A single layout, OR a comma-separated
 *   list of layouts to load simultaneously. The first is initial.
 *     "cz"          single layout
 *     "cz,us"       cz initial + us, switchable
 *     "cz,us,de"    three layouts, cycled in order
 *
 * WS_KBD_OPTIONS — XKB options string, NULL for none. Multiple
 *   options separated by commas. Common ones:
 *     "grp:win_space_toggle"   Win+Space cycles layouts
 *     "grp:alt_shift_toggle"   Alt+Shift cycles layouts
 *     "grp:caps_toggle"        Caps Lock cycles layouts
 *     "grp:menu_toggle"        Menu key cycles layouts
 *     "ctrl:nocaps"            map Caps Lock to Ctrl
 *   Combine: "grp:win_space_toggle,ctrl:nocaps"
 *
 * Workspace shortcuts (Win+1..9) are bound for both Czech and US
 * keyboard physical layouts in dwl/config.h, so they keep working
 * when you swap WS_KBD_LAYOUT or toggle between layouts at runtime.
 * ------------------------------------------------------------ */
#define WS_KBD_LAYOUT         "cz,us"
#define WS_KBD_OPTIONS        "grp:win_space_toggle"
#define WS_KEY_REPEAT_RATE    50     /* key repeats per second while held */
#define WS_KEY_REPEAT_DELAY   200    /* milliseconds before repeat begins */

/* ------------------------------------------------------------
 * TRACKPAD
 * ------------------------------------------------------------ */
#define WS_TRACKPAD_TAP            1    /* 1 = tap to click */
#define WS_TRACKPAD_DRAG           1    /* 1 = tap-and-drag */
#define WS_TRACKPAD_DRAG_LOCK      1    /* 1 = keep drag active after finger lift */
#define WS_TRACKPAD_NATURAL_SCROLL 0    /* 1 = reversed (natural) scroll direction */
#define WS_TRACKPAD_DISABLE_TYPING 1    /* 1 = disable pad while typing */
#define WS_TRACKPAD_ACCEL          0.0  /* pointer acceleration [-1.0 .. 1.0] */

/* ------------------------------------------------------------
 * BAR (dwlb)
 * ------------------------------------------------------------ */
#define WS_BAR_BOTTOM        0    /* 0 = bar at top, 1 = at bottom */
#define WS_BAR_HIDE_VACANT   1    /* 1 = hide tag slots with no windows */
#define WS_BAR_VPADDING      5    /* vertical padding above/below text (pixels) */
#define WS_BAR_CENTER_TITLE  1    /* 1 = center window title in middle segment */
#define WS_BAR_SCALE         1    /* HiDPI buffer scale factor (1 = normal, 2 = 2×) */

/* Workspace tag labels shown in the bar — 9 comma-separated strings. */
#define WS_TAG_NAMES  "1", "2", "3", "4", "5", "6", "7", "8", "9"

/* Left-status logo glyph (default: nf-linux-void U+F32E).
 * Any single Nerd Font glyph works here. */
#define WS_LEFTST_LOGO  "\xef\x8c\xae"

/* Clock + date strftime formats (left status). Examples:
 *   "%H:%M"         24-hour, leading zero  (default)
 *   "%I:%M %p"      12-hour with AM/PM
 *   "%H:%M:%S"      with seconds (still updates only once per minute)
 *   "%b %d"         "Jan 05"  (default)
 *   "%a %d.%m."     "Mon 05.01."  (european)
 *   "%Y-%m-%d"      "2026-01-05"
 */
#define WS_TIME_FMT     "%H:%M"
#define WS_DATE_FMT     "%b %d"

/* ------------------------------------------------------------
 * STATUS MODULES  (1 = show, 0 = hide)
 * ------------------------------------------------------------ */
#define WS_STATUS_DISK        1
#define WS_STATUS_CPU         1   /* includes CPU temperature when available */
#define WS_STATUS_BATTERY     1
#define WS_STATUS_VOLUME      1
#define WS_STATUS_VPN         1   /* mullvad WireGuard tunnel up/down */
#define WS_STATUS_WIFI        1

/* How often each metric is re-sampled (seconds, multiples of 1). */
#define WS_STATUS_CADENCE_WIFI    5    /* wifi signal strength */
#define WS_STATUS_CADENCE_BAT    30    /* battery level + charge state */
#define WS_STATUS_CADENCE_DISK  300    /* disk usage */

/* ------------------------------------------------------------
 * MULLVAD VPN
 *
 * The tunnel is brought up by scripts/mullvad-vpn (a wg-quick wrapper).
 * Bootstrap once with `sudo mullvad-wg-setup <ACCOUNT>` to register a
 * device and cache the relay catalogue. After that `mullvad-vpn up` is
 * called automatically at login by dwl-autostart, by the post-resume
 * elogind hook, and by mullvad-watchdog if the handshake goes stale.
 *
 * WS_VPN_KEEPALIVE: WireGuard PersistentKeepalive in seconds. Without
 *   this, NAT mappings on home routers expire after 30–180s of idle and
 *   the tunnel silently dies. Mullvad's own client uses 25.
 *
 * WS_VPN_STALE_S: handshake older than this (seconds) is treated as
 *   dead. The bar shield turns red, mullvad-watchdog reconnects on the
 *   next two consecutive checks. WireGuard rekeys every 120s while
 *   active, so 180 is conservative.
 *
 * WS_VPN_KILLSWITCH: 1 enables an nftables ruleset that drops outbound
 *   traffic that isn't (a) loopback, (b) the encrypted WG tunnel,
 *   (c) RFC1918/link-local LAN, (d) DHCP, or (e) the WG handshake to
 *   the *current* peer endpoint. Fails closed — when the tunnel is
 *   down or stale, you get no internet instead of a clear-text leak.
 *   Toggle off at runtime with `sudo mullvad-vpn killswitch off` for
 *   captive portals on public IPs.
 *
 * WS_VPN_WATCHDOG: 1 enables the mullvad-watchdog runit service, which
 *   updates /run/mullvad.handshake every 5s (read by dwlb-status for
 *   the bar shield's STALE state) and reconnects when the tunnel dies
 *   while intended state is "up".
 *
 * WS_VPN_REGION: preferred two-letter country code for relay selection.
 *   bring_up tries this region first, then a Central-Europe fallback
 *   set, then global ranking by ping.
 * ------------------------------------------------------------ */
#define WS_VPN_KEEPALIVE      25
#define WS_VPN_STALE_S       180
#define WS_VPN_KILLSWITCH      1
#define WS_VPN_WATCHDOG        1
#define WS_VPN_REGION       "de"

/* ------------------------------------------------------------
 * HUD WIDGET (ws-hud) — hover-revealed button panel that slides
 * down from the bar.  Launch with `ws-hud` (not autostarted).
 *
 * Colours below are ARGB (0xAARRGGBB) — note the alpha is the
 * top byte, unlike the WS_BAR_* colours which are RGBA.
 * ------------------------------------------------------------ */

/* colours */
#define WS_HUD_BG               0xcc000000u   /* panel background (matches WS_BAR_BG) */
#define WS_HUD_FG               0xff1e3a3au   /* outer + per-button border (=WS_BORDER) */
#define WS_HUD_ON               0xff3a7268u   /* toggle-on fill (=WS_FOCUS) */
#define WS_HUD_HOLD             0xff5fa090u   /* click-flash fill while held (distinct from ON so feedback is visible while toggled on) */
#define WS_HUD_WARN             0xffd04848u   /* toggle in warn state (e.g. VPN handshake stale) */
#define WS_HUD_ICON             0xffffffffu   /* nerd-font glyph colour */
#define WS_HUD_FONT             "FiraCode Nerd Font:size=18"

/* buttons — { TYPE, action, alt-action, warn-action, state-cmd, icon }
 *   TYPE         0 = click (one shot), 1 = toggle (alternates action/alt-action)
 *   action       shell command run on press (or on toggle-on); NULL = no-op
 *   alt-action   toggle-off command (ignored for click; NULL for click)
 *   warn-action  command run when clicked in WARN state (only meaningful for
 *                toggles whose state-cmd can return exit-2). NULL falls back
 *                to action. Used for the VPN shield: clicking a red (stale)
 *                shield fires a direct `mullvad-vpn reconnect` instead of
 *                re-opening the picker menu.
 *   state-cmd    optional probe run on ws-hud startup AND every 5s while the
 *                HUD is visible; exit 0 → ON, exit 2 → WARN, anything else
 *                → OFF. Only meaningful for type=1. NULL keeps state at OFF.
 *   icon         nerd-font codepoint shown in the button (0 = blank)
 * Add or remove rows freely; widget width auto-fits the count. */
#define WS_HUD_BUTTONS \
	{ 1, "pkill -x wlsunset; wlsunset -T 2801 -t 2800",  "pkill -x wlsunset",              NULL, "pgrep -fx 'wlsunset -T 2801 -t 2800' >/dev/null || { pgrep -x wlsunset >/dev/null && h=$(date +%H) && { [ $h -ge 20 ] || [ $h -lt 7 ]; }; }", 0xf186 }, /* moon — night mode. ON when (a) the override `wlsunset -T 2801 -t 2800` is running, OR (b) the scheduled wlsunset is running AND we're in the night window (20:00–07:00). Toggling ON kills the schedule and forces a flatter 2800K; toggling OFF kills wlsunset entirely until next dwl-autostart. */ \
	{ 1, "makoctl mode -a do-not-disturb", "makoctl mode -r do-not-disturb", NULL, NULL,                          0xf1f6 }, /* bell-slash — DND (needs the [mode=do-not-disturb] block in mako config) */ \
	{ 1, "ws-hud-lidlock on",              "ws-hud-lidlock off",             NULL, "ws-hud-lidlock status | grep -qx on", 0xf023 }, /* lock — inhibit lid-close hibernate (holds elogind handle-lid-switch lock) */ \
	{ 1, "foot -T ws-hud-mullvad --app-id=ws-hud-mullvad -e mullvad-menu", \
	     "sudo -n mullvad-vpn down && notify-send -a Mullvad -i network-vpn-disabled 'Mullvad VPN' 'Disconnected' || notify-send -a Mullvad -i dialog-warning 'Mullvad VPN' 'Disconnect failed (run mullvad-wg-setup?)'", \
	     "sudo -n mullvad-vpn reconnect && notify-send -a Mullvad -i network-vpn 'Mullvad VPN' 'Reconnected' || notify-send -a Mullvad -i dialog-warning 'Mullvad VPN' 'Reconnect failed (see /run/mullvad.log)'", \
	     "mullvad-vpn health",  0xf132 }, /* shield — Mullvad VPN; exit 0=on (green), 2=stale (red), 1=off. off→opens picker, on→disconnect, stale→direct reconnect (no menu) */ \
	{ 0, "foot -T ws-hud-bt   --app-id=ws-hud-bt   -e bluetuith --no-warning", NULL, NULL, NULL, 0xf293 }, /* bluetooth */ \
	{ 0, "foot -T ws-hud-wifi --app-id=ws-hud-wifi -e impala",                 NULL, NULL, NULL, 0xf1eb }, /* wifi */ \
	{ 0, "foot -T ws-hud-vol  --app-id=ws-hud-vol  -e pulsemixer",             NULL, NULL, NULL, 0xf028 }  /* volume */

/* geometry (pixels) */
#define WS_HUD_BTN_W            44   /* button width  */
#define WS_HUD_BTN_H            44   /* button height */
#define WS_HUD_BTN_GAP          8    /* horizontal gap between buttons */
#define WS_HUD_BTN_BORDER_PX    2    /* per-button border thickness */
#define WS_HUD_PAD              8    /* inner padding around the button row */
#define WS_HUD_OUTER_PX         2    /* panel outer border thickness */
#define WS_HUD_BAR_H            28   /* dwlb bar height — top strip stays transparent */
#define WS_HUD_BTN_OVERLAP      16   /* px buttons extend up into the bar zone */
#define WS_HUD_TRIG_H           5    /* hover-trigger strip height */

/* timing (milliseconds) */
#define WS_HUD_HIDE_DELAY_MS    30   /* deferred hide after pointer leave */
#define WS_HUD_CLICK_GRACE_MS   100  /* leaves within this long after a click are dropped */
#define WS_HUD_ANIM_TAU_MS      50.0 /* slide-animation time-constant (smaller = snappier) */
#define WS_HUD_ANIM_FRAME_MS    16   /* animation tick (~60Hz) */
#define WS_HUD_ANIM_EPSILON     0.5  /* px from target where animation snaps */

/* ------------------------------------------------------------
 * GTK / icon / cursor themes
 *
 * The installer fetches Graphite-gtk-theme (vinceliuice) and the
 * Bibata cursor release tarball when missing — the macros below are
 * only the *names* baked into ~/.config/gtk-{3,4}.0/settings.ini and
 * gsettings, NOT a generic theme switcher. Changing WS_GTK_THEME to
 * something the installer doesn't fetch will leave you with broken
 * fallbacks until you install that theme yourself.
 *
 * WS_PAPIRUS_FOLDER picks the folder accent (grey, white, teal, …);
 * see `papirus-folders -l` for the full list.
 * ------------------------------------------------------------ */
#define WS_GTK_THEME       "Graphite-Dark"
#define WS_ICON_THEME      "Papirus-Dark"
#define WS_CURSOR_THEME    "Bibata-Modern-Ice"
#define WS_CURSOR_SIZE     20
#define WS_GTK_FONT        "FiraCode Nerd Font 10"
#define WS_PAPIRUS_FOLDER  "teal"

/* ------------------------------------------------------------
 * APP COMMANDS
 * ------------------------------------------------------------ */
#define WS_TERM_CMD       "foot"
#define WS_LAUNCHER_CMD   "dwl-launcher"
#define WS_BROWSER_CMD    "librewolf"
#define WS_EDITOR_CMD     "code"
#define WS_LOCK_CMD       "twlctl lock"
#define WS_SCREENSHOT_CMD "$HOME/.local/bin/screenshot-area"
#define WS_OSD_CMD        "twlctl"   /* twl owns volume/brightness/mic — args: volume {up|down|mute} / mic mute / backlight {up|down} */
#define WS_POMODORO_CMD   "$HOME/.local/bin/ws-pomodoro" /* pomodoro timer + kew music */
#define WS_POWERMENU_CMD  "twlctl powermenu" /* built-in: poweroff/reboot/hibernate/logout (twl/config.h:POWERMENU_INIT) */
#define WS_OBSIDIAN_CMD     "obsidian"
#define WS_FILEMANAGER_CMD  "thunar"

/* ------------------------------------------------------------
 * KEYBINDS
 *
 * WS_MOD is the primary modifier for all compositor key combos.
 *   WLR_MODIFIER_LOGO  →  Win / Super key  (default)
 *   WLR_MODIFIER_ALT   →  Alt key
 *
 * Key symbols come from <xkbcommon/xkbcommon-keysyms.h>.
 * Shifted keysyms (capital letters like XKB_KEY_S) require
 * Shift to be held — the comment shows the full chord.
 *
 *   Win + Enter      →  terminal
 *   Win + d          →  launcher
 *   Win + s          →  browser  (librewolf)
 *   Win + c          →  editor   (code)
 *   Win + l          →  lock screen
 *   Win + Shift + L  →  lock screen (alt chord)
 *   Win + Shift + S  →  screenshot
 *   Win + p          →  pomodoro menu
 *   Win + e          →  power menu (poweroff/reboot/hibernate/logout)
 *   Win + o          →  obsidian
 *   Win + t          →  file manager (thunar)
 * ------------------------------------------------------------ */
#define WS_MOD            WLR_MODIFIER_LOGO

#define WS_KEY_TERM       XKB_KEY_Return   /* Win + Enter      */
#define WS_KEY_LAUNCHER   XKB_KEY_d        /* Win + d          */
#define WS_KEY_BROWSER    XKB_KEY_s        /* Win + s          */
#define WS_KEY_EDITOR     XKB_KEY_c        /* Win + c          */
#define WS_KEY_LOCK       XKB_KEY_l        /* Win + l          */
#define WS_KEY_LOCK_ALT   XKB_KEY_L        /* Win + Shift + L  */
#define WS_KEY_SCREENSHOT XKB_KEY_S        /* Win + Shift + S  */
#define WS_KEY_POMODORO   XKB_KEY_p        /* Win + p          */
#define WS_KEY_POWERMENU  XKB_KEY_e        /* Win + e          */
#define WS_KEY_OBSIDIAN     XKB_KEY_o      /* Win + o          */
#define WS_KEY_FILEMANAGER  XKB_KEY_t      /* Win + t          */

#endif /* DWLARP_CONFIG_H */
