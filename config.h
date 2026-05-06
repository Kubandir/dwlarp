/* ============================================================
 * wayland-suckless — single user-facing config.
 *
 * Edit values here, then re-run ./install.sh.  Everything
 * downstream (compositor, bar, status, lock screen) is driven
 * by these macros.  No other file should need touching.
 * ============================================================ */

#ifndef WAYLAND_SUCKLESS_CONFIG_H
#define WAYLAND_SUCKLESS_CONFIG_H

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
#define WS_GAP_PX          6       /* gap between tiled windows (pixels) */
#define WS_MFACT           0.55f   /* default master/stack split [0.05..0.95] */

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
#define WS_STATUS_BRIGHTNESS  1
#define WS_STATUS_WIFI        1

/* How often each metric is re-sampled (seconds, multiples of 1). */
#define WS_STATUS_CADENCE_WIFI    5    /* wifi signal strength */
#define WS_STATUS_CADENCE_BAT    30    /* battery level + charge state */
#define WS_STATUS_CADENCE_DISK  300    /* disk usage */

/* ------------------------------------------------------------
 * APP COMMANDS
 * ------------------------------------------------------------ */
#define WS_TERM_CMD       "foot"
#define WS_LAUNCHER_CMD   "dmenu-launcher"
#define WS_BROWSER_CMD    "helium"
#define WS_EDITOR_CMD     "code"
#define WS_LOCK_CMD       "swaylock"
#define WS_SCREENSHOT_CMD "$HOME/.local/bin/screenshot-area"
#define WS_OSD_CMD        "$HOME/.local/bin/dwl-osd"   /* volume/brightness/mic OSD */

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
 *   Win + s          →  browser  (helium)
 *   Win + c          →  editor   (code)
 *   Win + l          →  lock screen
 *   Win + Shift + L  →  lock screen (alt chord)
 *   Win + Shift + S  →  screenshot
 * ------------------------------------------------------------ */
#define WS_MOD            WLR_MODIFIER_LOGO

#define WS_KEY_TERM       XKB_KEY_Return   /* Win + Enter      */
#define WS_KEY_LAUNCHER   XKB_KEY_d        /* Win + d          */
#define WS_KEY_BROWSER    XKB_KEY_s        /* Win + s          */
#define WS_KEY_EDITOR     XKB_KEY_c        /* Win + c          */
#define WS_KEY_LOCK       XKB_KEY_l        /* Win + l          */
#define WS_KEY_LOCK_ALT   XKB_KEY_L        /* Win + Shift + L  */
#define WS_KEY_SCREENSHOT XKB_KEY_S        /* Win + Shift + S  */

#endif /* WAYLAND_SUCKLESS_CONFIG_H */
