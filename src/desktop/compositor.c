/* SPDX-License-Identifier: GPL-3.0-or-later */
/*
 * nirvana-desktop: asking the compositor for what Wayland gives a client no request for -
 * to put a window to the back (the depth gadget).
 *
 * labwc has the action ("Lower") but no command channel, and it does not let virtual
 * keyboards trigger its key bindings. Its mouse bindings work for every pointer, so
 * Nirvana's labwc settings (data/labwc/rc.xml) bind the "Task" button - one mice do not
 * have - over a window to Lower, and the desktop clicks that button on a virtual pointer
 * (wlr-virtual-pointer) while the real pointer is still on the gadget: labwc lowers the
 * window under it.
 *
 * Other compositors: false is returned and the caller does what it can without.
 */
#include "desktop.h"

#include <Ecore_Wl2.h>
#include <string.h>
#include <time.h>
#include <wayland-client.h>

#include "wlr-virtual-pointer-unstable-v1-client-protocol.h"

#define NV_BTN_TASK 0x117 /* (linux/input-event-codes.h) */

static struct {
    bool tried;
    struct zwlr_virtual_pointer_v1 *ptr;
    struct wl_display *display;
} vp;

static bool is_labwc(void)
{
    if (getenv("LABWC_PID"))
        return true;
    const char *d = getenv("XDG_CURRENT_DESKTOP");
    return d && strstr(d, "labwc");
}

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static bool vp_setup(void)
{
    if (vp.tried)
        return vp.ptr != NULL;
    vp.tried = true;
    Ecore_Wl2_Display *d = ecore_wl2_connected_display_get(NULL);
    if (!d)
        return false;
    vp.display = ecore_wl2_display_get(d);
    Eina_Iterator *it = ecore_wl2_display_globals_get(d);
    Ecore_Wl2_Global *g;
    unsigned id = 0;
    EINA_ITERATOR_FOREACH(it, g)
        if (!strcmp(g->interface, zwlr_virtual_pointer_manager_v1_interface.name))
            id = g->id;
    eina_iterator_free(it);
    if (!id)
        return false;
    struct zwlr_virtual_pointer_manager_v1 *mgr =
        wl_registry_bind(ecore_wl2_display_registry_get(d), id, &zwlr_virtual_pointer_manager_v1_interface, 1);
    /* (no seat given: the compositor's default seat, the one the real pointer is in) */
    vp.ptr = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(mgr, NULL);
    return vp.ptr != NULL;
}

bool nv_compositor_lower_under_pointer(void)
{
    if (nv.backend != NV_BACKEND_WAYLAND || !is_labwc() || !vp_setup())
        return false;
    uint32_t t = now_ms();
    zwlr_virtual_pointer_v1_button(vp.ptr, t, NV_BTN_TASK, WL_POINTER_BUTTON_STATE_PRESSED);
    zwlr_virtual_pointer_v1_frame(vp.ptr);
    zwlr_virtual_pointer_v1_button(vp.ptr, t + 1, NV_BTN_TASK, WL_POINTER_BUTTON_STATE_RELEASED);
    zwlr_virtual_pointer_v1_frame(vp.ptr);
    wl_display_flush(vp.display);
    return true;
}
