/* ============================================================
 * wayland-suckless — single user-facing config.
 *
 * Edit values in this file, then re-run ./install.sh. Everything
 * below this layer (compositor internals, build flags, keybind
 * plumbing) is referenced by macro and shouldn't need touching.
 * ============================================================ */

#ifndef WAYLAND_SUCKLESS_CONFIG_H
#define WAYLAND_SUCKLESS_CONFIG_H

/* ------------------------------------------------------------
 * COLORS  (0xRRGGBBAA — alpha for transparency where supported)
 * ------------------------------------------------------------ */
#define WS_BG              0x222222ff   /* desktop root */
#define WS_BORDER          0x1e3a3aff   /* unfocused window border */
#define WS_FOCUS           0x3a7268ff   /* focused window border, accents */
#define WS_URGENT          0xff0000ff   /* urgent/wrong-input flash */

#define WS_BAR_BG          0x000000cc   /* bar background */
#define WS_BAR_FG          0xffffffff   /* bar text */
#define WS_BAR_ACTIVE_BG   0x2a2f3acc   /* selected workspace tag */
#define WS_BAR_URGENT_BG   0xee3300ff

/* swaylock colors are written as hex strings (no 0x prefix). */
#define WS_LOCK_SCREEN_HEX "000000"
#define WS_LOCK_RING_HEX   "3a7268a6"
#define WS_LOCK_TEXT_HEX   "a8d5cc"
#define WS_LOCK_WRONG_HEX  "d06878a6"

/* ------------------------------------------------------------
 * FONTS
 * ------------------------------------------------------------ */
#define WS_BAR_FONT   "FiraCode Nerd Font:weight=bold:size=11, Symbols Nerd Font:size=12"
#define WS_LOCK_FONT  "FiraCode Nerd Font"
#define WS_LOCK_FONT_SIZE 14

/* ------------------------------------------------------------
 * WALLPAPER  (path relative to repo root; installed to
 *             ~/.local/share/dwl/wallpaper.png)
 * ------------------------------------------------------------ */
#define WS_WALLPAPER  "assets/wallpaper.png"

/* ------------------------------------------------------------
 * STATUS MODULES  (1 = show, 0 = hide)
 * ------------------------------------------------------------ */
#define WS_STATUS_DISK        1
#define WS_STATUS_CPU         1   /* incl. CPU temperature */
#define WS_STATUS_BATTERY     1
#define WS_STATUS_VOLUME      1
#define WS_STATUS_BRIGHTNESS  1
#define WS_STATUS_WIFI        1

/* ------------------------------------------------------------
 * APP COMMANDS
 * ------------------------------------------------------------ */
#define WS_TERM_CMD       "foot"
#define WS_LAUNCHER_CMD   "bemenu-desktop"
#define WS_BROWSER_CMD    "thorium"
#define WS_LOCK_CMD       "swaylock"
#define WS_SCREENSHOT_CMD "$HOME/.local/bin/screenshot-area"

/* ------------------------------------------------------------
 * KEYBINDS for the apps above.
 *
 * WS_MOD is the modifier shared by every Win+key combo.
 * WS_MOD = WLR_MODIFIER_LOGO  → Win/Super
 *          WLR_MODIFIER_ALT   → Alt
 *
 * Key symbols come from <xkbcommon/xkbcommon-keysyms.h>.
 * ------------------------------------------------------------ */
#define WS_MOD            WLR_MODIFIER_LOGO

#define WS_KEY_TERM       XKB_KEY_Return
#define WS_KEY_LAUNCHER   XKB_KEY_d
#define WS_KEY_BROWSER    XKB_KEY_s
#define WS_KEY_LOCK       XKB_KEY_l
#define WS_KEY_SCREENSHOT XKB_KEY_S        /* used with WS_MOD|SHIFT */

#endif /* WAYLAND_SUCKLESS_CONFIG_H */
