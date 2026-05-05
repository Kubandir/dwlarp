/* Taken from https://github.com/djpohly/dwl/issues/466 */
#define COLOR(hex)    { ((hex >> 24) & 0xFF) / 255.0f, \
                        ((hex >> 16) & 0xFF) / 255.0f, \
                        ((hex >> 8) & 0xFF) / 255.0f, \
                        (hex & 0xFF) / 255.0f }
/* appearance */
static const int sloppyfocus               = 1;
static const int bypass_surface_visibility = 0;
static const unsigned int borderpx         = 2;
static const unsigned int gappx            = 6;
static const float rootcolor[]             = COLOR(0x222222ff);
static const float bordercolor[]           = COLOR(0x1e3a3aff);
static const float focuscolor[]            = COLOR(0x3a7268ff);
static const float urgentcolor[]           = COLOR(0xff0000ff);
static const float fullscreen_bg[]         = {0.0f, 0.0f, 0.0f, 1.0f};
static const float resizepreviewcolor[]    = COLOR(0x3a7268cc); /* resize preview line color (RGBA) */
static const unsigned int resizepreviewpx  = 2;                  /* resize preview line thickness */
static const float movepreviewbordercolor[]= COLOR(0x3a7268cc); /* drop-target border (RGBA) */
static const float movepreviewbgcolor[]    = COLOR(0x3a726840); /* drop-target fill (more transparent) */
static const unsigned int movepreviewbw    = 2;                  /* drop-target border thickness */
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
	{ NULL, 0.55f, 1, 1, &layouts[0], WL_OUTPUT_TRANSFORM_NORMAL, -1, -1 },
};

/* keyboard */
static const struct xkb_rule_names xkb_rules = {
	.layout  = "cz",
	.options = NULL,
};

static const int repeat_rate = 25;
static const int repeat_delay = 600;

/* Trackpad */
static const int tap_to_click = 1;
static const int tap_and_drag = 1;
static const int drag_lock = 1;
static const int natural_scrolling = 0;
static const int disable_while_typing = 1;
static const int left_handed = 0;
static const int middle_button_emulation = 0;
static const enum libinput_config_scroll_method scroll_method = LIBINPUT_CONFIG_SCROLL_2FG;
static const enum libinput_config_click_method click_method = LIBINPUT_CONFIG_CLICK_METHOD_BUTTON_AREAS;
static const uint32_t send_events_mode = LIBINPUT_CONFIG_SEND_EVENTS_ENABLED;
static const enum libinput_config_accel_profile accel_profile = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
static const double accel_speed = 0.0;
static const enum libinput_config_tap_button_map button_map = LIBINPUT_CONFIG_TAP_MAP_LRM;

/* Win key as modifier */
#define MODKEY WLR_MODIFIER_LOGO

/* Czech QWERTZ: unshifted number row → shifted number row */
#define TAGKEYS(KEY,SKEY,TAG) \
	{ MODKEY,                    KEY,   view, {.ui = 1 << TAG} }, \
	{ MODKEY|WLR_MODIFIER_SHIFT, SKEY,  tag,  {.ui = 1 << TAG} }

#define SHCMD(cmd) { .v = (const char*[]){ "/bin/sh", "-c", cmd, NULL } }

static const char *termcmd[]    = { "foot", NULL };
static const char *menucmd[]    = { "bemenu-desktop", NULL };
static const char *firefoxcmd[] = { "thorium", NULL };

static const Key keys[] = {
	/* modifier                   key                      function           argument */

	/* applications */
	{ MODKEY,                     XKB_KEY_Return,          spawn,             {.v = termcmd} },
	{ MODKEY,                     XKB_KEY_d,               spawn,             {.v = menucmd} },
	{ MODKEY,                     XKB_KEY_s,               spawn,             {.v = firefoxcmd} },
	{ MODKEY|WLR_MODIFIER_SHIFT,  XKB_KEY_S,               spawn,             SHCMD("$HOME/.local/bin/screenshot-area") },

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

	/* Laptop function keys (no modifier) */
	{ 0, XKB_KEY_XF86AudioRaiseVolume, spawn, SHCMD("wpctl set-mute @DEFAULT_AUDIO_SINK@ 0; wpctl set-volume -l 1.5 @DEFAULT_AUDIO_SINK@ 5%+") },
	{ 0, XKB_KEY_XF86AudioLowerVolume, spawn, SHCMD("wpctl set-mute @DEFAULT_AUDIO_SINK@ 0; wpctl set-volume -l 1.5 @DEFAULT_AUDIO_SINK@ 5%-") },
	{ 0, XKB_KEY_XF86AudioMute,        spawn, SHCMD("wpctl set-mute @DEFAULT_AUDIO_SINK@ toggle") },
	{ 0, XKB_KEY_XF86AudioMicMute,     spawn, SHCMD("wpctl set-mute @DEFAULT_AUDIO_SOURCE@ toggle") },
	{ 0, XKB_KEY_XF86MonBrightnessUp,   spawn, SHCMD("brightnessctl set +5%") },
	{ 0, XKB_KEY_XF86MonBrightnessDown, spawn, SHCMD("brightnessctl set 5%-") },
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
