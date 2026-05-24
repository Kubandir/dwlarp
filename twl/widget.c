/* Widget abstraction: surface + layer-surface + SHM pool + ping-pong buffers. */
#include "twl.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>

Widget widgets[MAX_WIDGETS];

Widget *widget_alloc(WidgetKind k) {
    for (int i = 0; i < MAX_WIDGETS; i++)
        if (widgets[i].kind == W_NONE) {
            memset(&widgets[i], 0, sizeof widgets[i]);
            widgets[i].kind = k;
            widgets[i].pool_fd = -1;
            widgets[i].client_fd = -1;
            return &widgets[i];
        }
    return NULL;
}

Widget *widget_by_surface(uint32_t sid) {
    if (!sid) return NULL;
    for (int i = 0; i < MAX_WIDGETS; i++)
        if (widgets[i].kind != W_NONE && widgets[i].surface == sid)
            return &widgets[i];
    return NULL;
}
Widget *widget_by_ls(uint32_t lsid) {
    if (!lsid) return NULL;
    for (int i = 0; i < MAX_WIDGETS; i++)
        if (widgets[i].kind != W_NONE && widgets[i].layer_surface == lsid)
            return &widgets[i];
    return NULL;
}
Widget *widget_by_slock_surf(uint32_t id) {
    if (!id) return NULL;
    for (int i = 0; i < MAX_WIDGETS; i++)
        if (widgets[i].kind == W_LOCK && widgets[i].s.lock.slock_surf_id == id)
            return &widgets[i];
    return NULL;
}
Widget *widget_first(WidgetKind k) {
    for (int i = 0; i < MAX_WIDGETS; i++)
        if (widgets[i].kind == k) return &widgets[i];
    return NULL;
}

void widget_free_pool(Widget *w) {
    for (int i = 0; i < w->n_slots; i++) {
        if (w->slots[i].id) {
            wl_req(w->slots[i].id, BUFFER_REQ_DESTROY, NULL, 0, -1);
            w->slots[i].id = 0;
        }
    }
    if (w->id_pool) { wl_req(w->id_pool, POOL_REQ_DESTROY, NULL, 0, -1); w->id_pool = 0; }
    if (w->shm_base) { munmap(w->shm_base, w->pool_size); w->shm_base = NULL; }
    if (w->pool_fd >= 0) { close(w->pool_fd); w->pool_fd = -1; }
    w->pool_size = 0;
    w->n_slots = 0;
}

static void region_destroy(uint32_t rid) {
    if (rid) wl_req(rid, REGION_REQ_DESTROY, NULL, 0, -1);
}

void widget_destroy(Widget *w) {
    if (w->kind == W_NONE) return;
    widget_free_pool(w);
    region_destroy(w->input_region_id);
    w->input_region_id = 0;
    if (w->kind == W_HUD) {
        region_destroy(w->s.hud.region_trigger);
        region_destroy(w->s.hud.region_full);
        w->s.hud.region_trigger = w->s.hud.region_full = 0;
    }
    if (w->kind == W_LOCK && w->s.lock.slock_surf_id) {
        wl_req(w->s.lock.slock_surf_id, SLOCK_SURF_REQ_DESTROY, NULL, 0, -1);
        w->s.lock.slock_surf_id = 0;
    }
    if (w->layer_surface) {
        wl_req(w->layer_surface, LS_REQ_DESTROY, NULL, 0, -1);
        w->layer_surface = 0;
    }
    if (w->surface) {
        wl_req(w->surface, SURFACE_REQ_DESTROY, NULL, 0, -1);
        w->surface = 0;
    }
    if (w->client_fd >= 0) { close(w->client_fd); w->client_fd = -1; }
    /* Clear back-pointer from Output if this widget owned a slot there. */
    if (w->output) {
        Output *o = w->output;
        if (o->bar  == w) o->bar  = NULL;
        if (o->wall == w) o->wall = NULL;
        if (o->hud  == w) o->hud  = NULL;
        if (o->lock == w) o->lock = NULL;
        w->output = NULL;
    }
    w->kind = W_NONE;
}

void widget_setup_surface(Widget *w, uint32_t layer, const char *ns, Output *o) {
    w->output        = o;
    w->surface       = wl_new_id();
    w->layer_surface = wl_new_id();
    uint32_t sa = w->surface;
    wl_req(id_compositor, COMPOSITOR_REQ_CREATE_SURFACE, &sa, 1, -1);
    uint32_t b[64];
    /* layer-shell.get_layer_surface(new_id, surface, output, layer, ns) — wire
       order: id, surface, output, layer, ns. Manual build because mid-position
       string + uint32 layer. Output is mandatory on dwl's layer-shell v3. */
    b[0] = id_layer_shell;
    b[2] = w->layer_surface;
    b[3] = w->surface;
    b[4] = o ? o->wl_output : 0;
    b[5] = layer;
    int sl = (int)strlen(ns) + 1, pl = pad4(sl);
    b[6] = sl;
    memset((char *)&b[7], 0, pl);
    memcpy((char *)&b[7], ns, sl);
    int p = 7 + pl / 4;
    uint32_t size = p * 4;
    b[1] = (size << 16) | LAYER_SHELL_REQ_GET_LAYER_SURFACE;
    wl_send(b, size, -1);
}

void widget_set_size(Widget *w, int width, int height) {
    uint32_t a[2] = { (uint32_t)width, (uint32_t)height };
    wl_req(w->layer_surface, LS_REQ_SET_SIZE, a, 2, -1);
}
void widget_set_anchor(Widget *w, uint32_t bits) {
    wl_req(w->layer_surface, LS_REQ_SET_ANCHOR, &bits, 1, -1);
}
void widget_set_exclusive_zone(Widget *w, int zone) {
    uint32_t a = (uint32_t)zone; wl_req(w->layer_surface, LS_REQ_SET_EXCLUSIVE_ZONE, &a, 1, -1);
}
void widget_set_margin(Widget *w, int top, int right, int bot, int left) {
    uint32_t a[4] = { (uint32_t)top, (uint32_t)right, (uint32_t)bot, (uint32_t)left };
    wl_req(w->layer_surface, LS_REQ_SET_MARGIN, a, 4, -1);
}
void widget_set_kbd_interactive(Widget *w, int on) {
    uint32_t a = on; wl_req(w->layer_surface, LS_REQ_SET_KEYBOARD_INTERACTIVITY, &a, 1, -1);
}
void widget_set_input_region(Widget *w, uint32_t region_id) {
    uint32_t a = region_id;
    wl_req(w->surface, /*SET_INPUT_REGION=*/5, &a, 1, -1);
}

/* compositor.create_region + region.add(x,y,w,h). Returns new region id.
 * Caller is responsible for destroying the region (either explicitly or via
 * widget_set_input_region_rect, which manages a single live region per widget). */
uint32_t widget_make_region(int x, int y, int w, int h) {
    uint32_t rid = wl_new_id();
    uint32_t a = rid;
    wl_req(id_compositor, COMPOSITOR_REQ_CREATE_REGION, &a, 1, -1);
    uint32_t args[4] = { (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h };
    wl_req(rid, REGION_REQ_ADD, args, 4, -1);
    return rid;
}

void widget_set_input_region_rect(Widget *w, int x, int y, int ww, int hh) {
    uint32_t rid = widget_make_region(x, y, ww, hh);
    widget_set_input_region(w, rid);
    region_destroy(w->input_region_id);
    w->input_region_id = rid;
}

void widget_ensure_pool(Widget *w, int n_slots) {
    if (w->w <= 0 || w->h <= 0 || n_slots <= 0) return;
    int stride = w->w * 4;
    int one = stride * w->h;
    int total = one * n_slots;
    if (w->pool_size == total && w->n_slots == n_slots) return;
    widget_free_pool(w);

    w->pool_fd = syscall(SYS_memfd_create, "twl-w", 1u);
    if (w->pool_fd < 0) die("memfd_create: %s", strerror(errno));
    if (ftruncate(w->pool_fd, total) < 0) die("ftruncate: %s", strerror(errno));
    w->shm_base = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED, w->pool_fd, 0);
    if (w->shm_base == MAP_FAILED) die("mmap: %s", strerror(errno));
    w->pool_size = total;

    w->id_pool = wl_new_id();
    uint32_t pa[2] = { w->id_pool, (uint32_t)total };
    wl_req(id_shm, SHM_REQ_CREATE_POOL, pa, 2, w->pool_fd);
    w->n_slots = n_slots;
    for (int i = 0; i < n_slots; i++) {
        w->slots[i].id = wl_new_id();
        w->slots[i].px = (uint32_t *)(w->shm_base + one * i);
        w->slots[i].busy = 0;
        w->slots[i].off = one * i;
        uint32_t ba[6] = { w->slots[i].id, (uint32_t)w->slots[i].off,
                           (uint32_t)w->w, (uint32_t)w->h,
                           (uint32_t)stride, WL_SHM_FORMAT_ARGB8888 };
        wl_req(w->id_pool, POOL_REQ_CREATE_BUFFER, ba, 6, -1);
    }
}

BufSlot *widget_free_slot(Widget *w) {
    for (int i = 0; i < w->n_slots; i++)
        if (!w->slots[i].busy) return &w->slots[i];
    return NULL;
}

void widget_attach(Widget *w, BufSlot *s, int request_frame) {
    uint32_t at[3] = { s->id, 0, 0 };
    wl_req(w->surface, SURFACE_REQ_ATTACH, at, 3, -1);
    uint32_t dm[4] = { 0, 0, (uint32_t)w->w, (uint32_t)w->h };
    wl_req(w->surface, SURFACE_REQ_DAMAGE, dm, 4, -1);
    if (request_frame && !w->frame_cb) {
        w->frame_cb = wl_new_id();
        uint32_t a = w->frame_cb;
        wl_req(w->surface, SURFACE_REQ_FRAME, &a, 1, -1);
    }
    wl_req(w->surface, SURFACE_REQ_COMMIT, NULL, 0, -1);
    s->busy = 1;
    /* Reset any pending "free pool when idle" flag — the surface is no longer
     * idle. Callers that want a one-shot free re-set the flag *after* this
     * returns. Doing it here (rather than in widget_ensure_pool) avoids
     * stranding the flag when a render path early-returns without attaching. */
    w->want_pool_free = 0;
}

/* Buffer release: clear busy flag for matching slot. If the widget has been
 * marked for pool teardown (e.g. OSD after the last slab dismissed), free the
 * pool once every slot has been returned by the compositor. */
void on_buffer_release(uint32_t buf_id) {
    for (int i = 0; i < MAX_WIDGETS; i++) {
        Widget *w = &widgets[i];
        if (w->kind == W_NONE) continue;
        for (int j = 0; j < w->n_slots; j++)
            if (w->slots[j].id == buf_id) {
                w->slots[j].busy = 0;
                if (w->want_pool_free) {
                    int all_free = 1;
                    for (int k = 0; k < w->n_slots; k++)
                        if (w->slots[k].busy) { all_free = 0; break; }
                    if (all_free) {
                        w->want_pool_free = 0;
                        widget_free_pool(w);
                    }
                }
                return;
            }
    }
}
