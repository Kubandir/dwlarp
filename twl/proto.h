/* Raw Wayland wire opcodes — hand-extracted from the protocol XML.
 * Keep this file small; only what twl actually uses. */
#ifndef TWL_PROTO_H
#define TWL_PROTO_H

/* Fixed object ids */
#define ID_DISPLAY  1u
#define ID_REGISTRY 2u

/* wl_display events */
#define DISPLAY_EV_ERROR     0
#define DISPLAY_EV_DELETE_ID 1

/* wl_display requests */
#define DISPLAY_REQ_SYNC         0
#define DISPLAY_REQ_GET_REGISTRY 1

/* wl_registry */
#define REGISTRY_REQ_BIND      0
#define REGISTRY_EV_GLOBAL     0
#define REGISTRY_EV_GLOBAL_REM 1

/* wl_callback */
#define CALLBACK_EV_DONE 0

/* wl_compositor */
#define COMPOSITOR_REQ_CREATE_SURFACE 0
#define COMPOSITOR_REQ_CREATE_REGION  1

/* wl_region */
#define REGION_REQ_DESTROY  0
#define REGION_REQ_ADD      1
#define REGION_REQ_SUBTRACT 2

/* wl_shm */
#define SHM_REQ_CREATE_POOL 0

/* wl_shm_pool */
#define POOL_REQ_CREATE_BUFFER 0
#define POOL_REQ_DESTROY       1
#define POOL_REQ_RESIZE        2

/* wl_buffer */
#define BUFFER_REQ_DESTROY 0
#define BUFFER_EV_RELEASE  0

/* wl_surface */
#define SURFACE_REQ_DESTROY        0
#define SURFACE_REQ_ATTACH         1
#define SURFACE_REQ_DAMAGE         2
#define SURFACE_REQ_FRAME          3
#define SURFACE_REQ_COMMIT         6
#define SURFACE_REQ_DAMAGE_BUFFER  9

/* wl_seat */
#define SEAT_REQ_GET_POINTER  0
#define SEAT_REQ_GET_KEYBOARD 1
#define SEAT_EV_CAPABILITIES  0
#define SEAT_EV_NAME          1
#define SEAT_CAP_POINTER  1
#define SEAT_CAP_KEYBOARD 2

/* wl_output */
#define OUTPUT_EV_GEOMETRY 0
#define OUTPUT_EV_MODE     1
#define OUTPUT_EV_DONE     2
#define OUTPUT_EV_SCALE    3

/* zwlr_layer_shell_v1 */
#define LAYER_SHELL_REQ_GET_LAYER_SURFACE 0
#define LAYER_SHELL_REQ_DESTROY           1
#define LAYER_BACKGROUND 0
#define LAYER_BOTTOM     1
#define LAYER_TOP        2
#define LAYER_OVERLAY    3

/* zwlr_layer_surface_v1 */
#define LS_REQ_SET_SIZE                  0
#define LS_REQ_SET_ANCHOR                1
#define LS_REQ_SET_EXCLUSIVE_ZONE        2
#define LS_REQ_SET_MARGIN                3
#define LS_REQ_SET_KEYBOARD_INTERACTIVITY 4
#define LS_REQ_ACK_CONFIGURE             6
#define LS_REQ_DESTROY                   7
#define LS_EV_CONFIGURE 0
#define LS_EV_CLOSED    1
#define LS_ANCHOR_TOP    1
#define LS_ANCHOR_BOTTOM 2
#define LS_ANCHOR_LEFT   4
#define LS_ANCHOR_RIGHT  8

/* xdg_wm_base */
#define WM_BASE_REQ_DESTROY          0
#define WM_BASE_REQ_CREATE_POSITIONER 1
#define WM_BASE_REQ_GET_XDG_SURFACE  2
#define WM_BASE_REQ_PONG             3
#define WM_BASE_EV_PING              0

/* wl_shm pixel format */
#define WL_SHM_FORMAT_XRGB8888 1
#define WL_SHM_FORMAT_ARGB8888 0

/* zwlr_gamma_control_manager_v1 — night-mode color temperature. */
#define GAMMA_MGR_REQ_GET_GAMMA_CONTROL 0
#define GAMMA_MGR_REQ_DESTROY           1
/* zwlr_gamma_control_v1 */
#define GAMMA_CTRL_REQ_SET_GAMMA 0
#define GAMMA_CTRL_REQ_DESTROY   1
#define GAMMA_CTRL_EV_GAMMA_SIZE 0
#define GAMMA_CTRL_EV_FAILED     1

/* ext_session_lock_manager_v1 / ext_session_lock_v1 / ext_session_lock_surface_v1 */
#define SLOCK_MGR_REQ_DESTROY 0
#define SLOCK_MGR_REQ_LOCK    1
#define SLOCK_REQ_DESTROY            0
#define SLOCK_REQ_GET_LOCK_SURFACE   1
#define SLOCK_REQ_UNLOCK_AND_DESTROY 2
#define SLOCK_EV_LOCKED   0
#define SLOCK_EV_FINISHED 1
#define SLOCK_SURF_REQ_DESTROY        0
#define SLOCK_SURF_REQ_ACK_CONFIGURE  1
#define SLOCK_SURF_EV_CONFIGURE       0

/* zdwl_ipc_manager_v2 / zdwl_ipc_output_v2 — tag/title push from dwl. */
#define DWL_MGR_REQ_RELEASE          0
#define DWL_MGR_REQ_GET_OUTPUT       1
#define DWL_MGR_EV_TAGS              0
#define DWL_MGR_EV_LAYOUT            1
#define DWL_OUT_REQ_RELEASE          0
#define DWL_OUT_EV_TOGGLE_VISIBILITY 0
#define DWL_OUT_EV_ACTIVE            1
#define DWL_OUT_EV_TAG               2
#define DWL_OUT_EV_LAYOUT            3
#define DWL_OUT_EV_TITLE             4
#define DWL_OUT_EV_APPID             5
#define DWL_OUT_EV_LAYOUT_SYMBOL     6
#define DWL_OUT_EV_FRAME             7
#define DWL_OUT_TAG_NONE   0
#define DWL_OUT_TAG_ACTIVE 1
#define DWL_OUT_TAG_URGENT 2

#endif
