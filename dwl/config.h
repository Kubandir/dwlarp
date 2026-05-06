/* User-facing knobs live in the repo-root config.h.
 * This file wires those macros into dwl's internal constants. */
#include "../config.h"

/* Taken from https://github.com/djpohly/dwl/issues/466 */
#define COLOR(hex)    { ((hex >> 24) & 0xFF) / 255.0f, \
                        ((hex >> 16) & 0xFF) / 255.0f, \
                        ((hex >> 8) & 0xFF) / 255.0f, \
                        (hex & 0xFF) / 255.0f }
/* Build a translucent variant of a 0xRRGGBBAA color for previews.
 * wlroots scene rects expect *premultiplied* alpha, so multiply each
 * channel by (a/255) — otherwise the rect renders washed-out / black
 * when stacked above opaque windows. */
#define COLOR_ALPHA(hex, a) {                                          \
        ((((hex) >> 24) & 0xFF) / 255.0f) * ((a) / 255.0f),            \
        ((((hex) >> 16) & 0xFF) / 255.0f) * ((a) / 255.0f),            \
        ((((hex) >>  8) & 0xFF) / 255.0f) * ((a) / 255.0f),            \
        ((a) / 255.0f) }

/* appearance */
static const int sloppyfocus               = WS_SLOPPY_FOCUS;
static const int bypass_surface_visibility = 0;
static const unsigned int borderpx         = WS_BORDER_PX;
static const unsigned int gappx            = WS_GAP_PX;
static const float rootcolor[]             = COLOR(WS_BG);
static const float bordercolor[]           = COLOR(WS_BORDER);
static const float focuscolor[]            = COLOR(WS_FOCUS);
static const float urgentcolor[]           = COLOR(WS_URGENT);
static const float fullscreen_bg[]         = {0.0f, 0.0f, 0.0f, 1.0f};
static const float resizepreviewcolor[]    = COLOR_ALPHA(WS_FOCUS, 0xcc);
static const unsigned int resizepreviewpx  = 2;
static const float movepreviewbordercolor[]= COLOR_ALPHA(WS_FOCUS, 0xcc);
static const float movepreviewbgcolor[]    = COLOR_ALPHA(WS_FOCUS, 0x40);
static const unsigned int movepreviewbw    = 2;
static const float resize_factor           = 0.0002f;
static const uint32_t resize_interval_ms   = 16;

enum Direction { DIR_LEFT, DIR_RIGHT, DIR_UP, DIR_DOWN };
#define TAGCOUNT (9)

/* logging */
static int log_level = WLR_ERROR;

static const Rule rules[] = {
	/* app_id  title  tags mask  isfloating  monitor */
	{ NULL,    NULL,  0,         0,          -1 },
};

/* layout(s) */
static const Layout layouts[] = {
	{ "|w|",  btrtile },  /* true BSP — default */
	{ "[]=",  tile },
	{ "><>",  NULL },
	{ "[M]",  monocle },
};

/* monitors */
static const MonitorRule monrules[] = {
	{ NULL, WS_MFACT, 1, 1, &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL, -1, -1 },
};

/* keyboard */
static const struct xkb_rule_names xkb_rules = {
	.layout  = WS_KBD_LAYOUT,
	.options = WS_KBD_OPTIONS,
};

static const int repeat_rate  = WS_KEY_REPEAT_RATE;
static const int repeat_delay = WS_KEY_REPEAT_DELAY;

/* trackpad */
static const int tap_to_click          = WS_TRACKPAD_TAP;
static const int tap_and_drag          = WS_TRACKPAD_DRAG;
static const int drag_lock             = WS_TRACKPAD_DRAG_LOCK;
static const int natural_scrolling     = WS_TRACKPAD_NATURAL_SCROLL;
static const int disable_while_typing  = WS_TRACKPAD_DISABLE_TYPING;
static const int left_handed           = 0;
static const int middle_button_emulation = 0;
static const enum libinput_config_scroll_method scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;
static const enum libinput_config_click_method click_method   = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
static const uint32_t send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
static const enum libinput_config_accel_profile accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
static const double accel_speed        = WS_TRACKPAD_ACCEL;
static const enum libinput_config_tap_button_map button_map   = LIBINPUT_CONFIG_TAP_MAP_LRM;

/* MODKEY comes from WS_MOD in the root config.h. */
#define MODKEY WS_MOD

/* Czech QWERTZ: unshifted number row → shifted number row.
 * Win+Number switches to workspace; Win+Shift+Number moves the focused
 * window there and follows it (tagandview). */
#define TAGKEYS(KEY,SKEY,TAG) \
	{ MODKEY,                    KEY,   view,       {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_SHIFT, SKEY,  tagandview, {.ui = 1 << TAG} }

#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

static const char *termcmd[]     = { WS_TERM_CMD,     NULL };
static const char *launchercmd[] = { WS_LAUNCHER_CMD, NULL };
static const char *browsercmd[]  = { WS_BROWSER_CMD,  NULL };
static const char *editorcmd[]   = { WS_EDITOR_CMD,   NULL };

static const Key keys[] = {
	/* modifier                   key                      function           argument */

	/* applications */
	{ MODKEY,                     WS_KEY_TERM,             spawn,             {.v = termcmd} },
	{ MODKEY,                     WS_KEY_LAUNCHER,         spawn,             {.v = launchercmd} },
	{ MODKEY,                     WS_KEY_BROWSER,          spawn,             {.v = browsercmd} },
	{ MODKEY,                     WS_KEY_EDITOR,           spawn,             {.v = editorcmd} },
	{ MODKEY,                     WS_KEY_LOCK,             spawn,             SHCMD(WS_LOCK_CMD) },
	{ MODKEY|WLR_MODIFIER_SHIFT,  WS_KEY_LOCK_ALT,         spawn,             SHCMD(WS_LOCK_CMD) },
	{ MODKEY|WLR_MODIFIER_SHIFT,  WS_KEY_SCREENSHOT,       spawn,             SHCMD(WS_SCREENSHOT_CMD) },

	/* window management */
	{ MODKEY,                     XKB_KEY_q,               killclient,        {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT,  XKB_KEY_Q,               quit,              {0} },
	{ MODKEY,                     XKB_KEY_e,               quit,              {0} },
	{ MODKEY,                     XKB_KEY_f,               togglefloating,    {0} },
	{ MODKEY|WLR_MODIFIER_SHIFT,  XKB_KEY_F,               togglefullscreen,  {0} },

	/* focus — arrow keys cycle through windows */
	{ MODKEY,                     XKB_KEY_Up,              focusstack,        {.i = -1} },
	{ MODKEY,                     XKB_KEY_Down,            focusstack,        {.i = +1} },
	{ MODKEY,                     XKB_KEY_Left,            focusstack,        {.i = -1} },
	{ MODKEY,                     XKB_KEY_Right,           focusstack,        {.i = +1} },

	/* BSP resize — keyboard nudge */
	{ MODKEY|WLR_MODIFIER_SHIFT,  XKB_KEY_Right,           setratio_h,        {.f = +0.025f} },
	{ MODKEY|WLR_MODIFIER_SHIFT,  XKB_KEY_Left,            setratio_h,        {.f = -0.025f} },
	{ MODKEY|WLR_MODIFIER_SHIFT,  XKB_KEY_Up,              setratio_v,        {.f = -0.025f} },
	{ MODKEY|WLR_MODIFIER_SHIFT,  XKB_KEY_Down,            setratio_v,        {.f = +0.025f} },

	/* workspaces — Czech QWERTZ number row (unshifted → shifted = digit) */
	TAGKEYS(XKB_KEY_plus,         XKB_KEY_1,               0),
	TAGKEYS(XKB_KEY_ecaron,       XKB_KEY_2,               1),
	TAGKEYS(XKB_KEY_scaron,       XKB_KEY_3,               2),
	TAGKEYS(XKB_KEY_ccaron,       XKB_KEY_4,               3),
	TAGKEYS(XKB_KEY_rcaron,       XKB_KEY_5,               4),
	TAGKEYS(XKB_KEY_zcaron,       XKB_KEY_6,               5),
	TAGKEYS(XKB_KEY_yacute,       XKB_KEY_7,               6),
	TAGKEYS(XKB_KEY_aacute,       XKB_KEY_8,               7),
	TAGKEYS(XKB_KEY_iacute,       XKB_KEY_9,               8),

	/* laptop function keys (no modifier) */
	{ 0, XKB_KEY_XF86AudioRaiseVolume,  spawn, SHCMD("$HOME/.local/bin/dwl-osd volume up") },
	{ 0, XKB_KEY_XF86AudioLowerVolume,  spawn, SHCMD("$HOME/.local/bin/dwl-osd volume down") },
	{ 0, XKB_KEY_XF86AudioMute,         spawn, SHCMD("$HOME/.local/bin/dwl-osd volume mute") },
	{ 0, XKB_KEY_XF86AudioMicMute,      spawn, SHCMD("$HOME/.local/bin/dwl-osd mic mute") },
	{ 0, XKB_KEY_XF86MonBrightnessUp,   spawn, SHCMD("$HOME/.local/bin/dwl-osd brightness up") },
	{ 0, XKB_KEY_XF86MonBrightnessDown, spawn, SHCMD("$HOME/.local/bin/dwl-osd brightness down") },
	{ 0, XKB_KEY_XF86AudioPlay,  spawn, SHCMD("playerctl play-pause") },
	{ 0, XKB_KEY_XF86AudioPause, spawn, SHCMD("playerctl play-pause") },
	{ 0, XKB_KEY_XF86AudioNext,  spawn, SHCMD("playerctl next") },
	{ 0, XKB_KEY_XF86AudioPrev,  spawn, SHCMD("playerctl previous") },

	/* VT switching — essential to avoid lockout */
	{ WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_Terminate_Server, quit, {0} },
#define CHVT(n) { WLR_MODIFIER_CTRL|WLR_MODIFIER_ALT, XKB_KEY_XF86Switch_VT_##n, chvt, {.ui = (n)} }
	CHVT(1), CHVT(2), CHVT(3), CHVT(4), CHVT(5), CHVT(6),
	CHVT(7), CHVT(8), CHVT(9), CHVT(10), CHVT(11), CHVT(12),
};

static const Button buttons[] = {
	{ MODKEY, BTN_LEFT,   moveresize,     {.ui = CurMove} },
	{ MODKEY, BTN_MIDDLE, togglefloating, {0} },
	{ MODKEY, BTN_RIGHT,  moveresize,     {.ui = CurResize} },
};
