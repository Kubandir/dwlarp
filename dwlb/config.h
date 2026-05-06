/* User-facing knobs live in the repo-root config.h.
 * This file wires those macros into dwlb's internal settings. */
#include "../config.h"

#define HEX_COLOR(hex)				\
	{ .red   = ((hex >> 24) & 0xff) * 257,	\
	  .green = ((hex >> 16) & 0xff) * 257,	\
	  .blue  = ((hex >> 8) & 0xff) * 257,	\
	  .alpha = (hex & 0xff) * 257 }

static bool ipc             = true;
static bool hidden          = false;
static bool bottom          = false;
static bool hide_vacant     = WS_BAR_HIDE_VACANT;
static uint32_t vertical_padding = WS_BAR_VPADDING;
static bool status_commands = true;
static bool center_title    = WS_BAR_CENTER_TITLE;
static bool custom_title    = true;
static bool active_color_title = false;
static uint32_t buffer_scale   = WS_BAR_SCALE;
static char *fontstr        = WS_BAR_FONT;
static char *tags_names[]   = { WS_TAG_NAMES };

static pixman_color_t active_fg_color          = HEX_COLOR(WS_BAR_FG);
static pixman_color_t active_bg_color          = HEX_COLOR(WS_BAR_ACTIVE_BG);
static pixman_color_t occupied_fg_color        = HEX_COLOR(WS_BAR_FG);
static pixman_color_t occupied_bg_color        = HEX_COLOR(WS_BAR_BG);
static pixman_color_t inactive_fg_color        = HEX_COLOR(WS_BAR_FG);
static pixman_color_t inactive_bg_color        = HEX_COLOR(WS_BAR_BG);
static pixman_color_t urgent_fg_color          = HEX_COLOR(WS_BAR_FG);
static pixman_color_t urgent_bg_color          = HEX_COLOR(WS_BAR_URGENT_BG);
static pixman_color_t middle_bg_color          = HEX_COLOR(WS_BAR_BG);
static pixman_color_t middle_bg_color_selected = HEX_COLOR(WS_BAR_BG);
