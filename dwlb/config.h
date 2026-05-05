#define HEX_COLOR(hex)				\
	{ .red   = ((hex >> 24) & 0xff) * 257,	\
	  .green = ((hex >> 16) & 0xff) * 257,	\
	  .blue  = ((hex >> 8) & 0xff) * 257,	\
	  .alpha = (hex & 0xff) * 257 }

static bool ipc = true;
static bool hidden = false;
static bool bottom = false;
static bool hide_vacant = true;
static uint32_t vertical_padding = 5;
static bool status_commands = true;
static bool center_title = true;
static bool custom_title = true;
static bool active_color_title = false;
static uint32_t buffer_scale = 1;
static char *fontstr = "FiraCode Nerd Font:weight=bold:size=11, Symbols Nerd Font:size=12";
static char *tags_names[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

/* Semi-transparent black bar; active workspace has dark gray bg */
static pixman_color_t active_fg_color          = HEX_COLOR(0xffffffff);
static pixman_color_t active_bg_color          = HEX_COLOR(0x2a2f3acc);
static pixman_color_t occupied_fg_color        = HEX_COLOR(0xffffffff);
static pixman_color_t occupied_bg_color        = HEX_COLOR(0x000000cc);
static pixman_color_t inactive_fg_color        = HEX_COLOR(0xffffffff);
static pixman_color_t inactive_bg_color        = HEX_COLOR(0x000000cc);
static pixman_color_t urgent_fg_color          = HEX_COLOR(0xffffffff);
static pixman_color_t urgent_bg_color          = HEX_COLOR(0xee3300ff);
static pixman_color_t middle_bg_color          = HEX_COLOR(0x000000cc);
static pixman_color_t middle_bg_color_selected = HEX_COLOR(0x000000cc);
