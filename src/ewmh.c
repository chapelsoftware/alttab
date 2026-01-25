/*
 * alttab - Alt+Tab window switcher
 * ewmh.c - EWMH protocol helpers
 */

#define _POSIX_C_SOURCE 200809L

#include "main.h"

static EwmhAtoms atoms_storage;

void ewmh_init_atoms(AppContext *ctx)
{
    EwmhAtoms *a = &atoms_storage;

    a->net_client_list = XInternAtom(ctx->display, "_NET_CLIENT_LIST", False);
    a->net_client_list_stacking = XInternAtom(ctx->display, "_NET_CLIENT_LIST_STACKING", False);
    a->net_current_desktop = XInternAtom(ctx->display, "_NET_CURRENT_DESKTOP", False);
    a->net_wm_desktop = XInternAtom(ctx->display, "_NET_WM_DESKTOP", False);
    a->net_wm_name = XInternAtom(ctx->display, "_NET_WM_NAME", False);
    a->net_wm_icon = XInternAtom(ctx->display, "_NET_WM_ICON", False);
    a->net_wm_state = XInternAtom(ctx->display, "_NET_WM_STATE", False);
    a->net_wm_state_hidden = XInternAtom(ctx->display, "_NET_WM_STATE_HIDDEN", False);
    a->net_wm_window_type = XInternAtom(ctx->display, "_NET_WM_WINDOW_TYPE", False);
    a->net_wm_window_type_normal = XInternAtom(ctx->display, "_NET_WM_WINDOW_TYPE_NORMAL", False);
    a->net_wm_window_type_dialog = XInternAtom(ctx->display, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    a->net_active_window = XInternAtom(ctx->display, "_NET_ACTIVE_WINDOW", False);
    a->utf8_string = XInternAtom(ctx->display, "UTF8_STRING", False);
    a->wm_name = XInternAtom(ctx->display, "WM_NAME", False);

    ctx->atoms = a;
}

/*
 * Generic property getter with error handling.
 * Returns property data on success (caller must XFree), NULL on failure.
 * Sets *nitems to the number of items returned.
 */
static unsigned char *ewmh_get_window_property(AppContext *ctx, Window win,
                                                Atom property, Atom req_type,
                                                unsigned long *nitems)
{
    Atom actual_type;
    int actual_format;
    unsigned long bytes_after;
    unsigned char *data = NULL;

    *nitems = 0;

    int status = XGetWindowProperty(ctx->display, win, property,
                                    0, ~0L, False, req_type,
                                    &actual_type, &actual_format,
                                    nitems, &bytes_after, &data);

    if (status != Success || actual_type == None) {
        if (data)
            XFree(data);
        *nitems = 0;
        return NULL;
    }

    return data;
}

Window *ewmh_get_client_list(AppContext *ctx, int *count)
{
    unsigned long nitems;
    Window *data;

    *count = 0;

    /* Try _NET_CLIENT_LIST_STACKING first (gives proper stacking order) */
    data = (Window *)ewmh_get_window_property(ctx, ctx->root,
                                               ctx->atoms->net_client_list_stacking,
                                               XA_WINDOW, &nitems);
    if (data && nitems > 0) {
        *count = (int)nitems;
        /* Caller must XFree the returned data */
        return data;
    }

    if (data)
        XFree(data);

    /* Fallback to _NET_CLIENT_LIST */
    data = (Window *)ewmh_get_window_property(ctx, ctx->root,
                                               ctx->atoms->net_client_list,
                                               XA_WINDOW, &nitems);
    if (data && nitems > 0) {
        *count = (int)nitems;
        return data;
    }

    if (data)
        XFree(data);

    return NULL;
}

int ewmh_get_current_desktop(AppContext *ctx)
{
    unsigned long nitems;
    unsigned long *data;

    data = (unsigned long *)ewmh_get_window_property(ctx, ctx->root,
                                                      ctx->atoms->net_current_desktop,
                                                      XA_CARDINAL, &nitems);
    if (data && nitems > 0) {
        int desktop = (int)*data;
        XFree(data);
        return desktop;
    }

    if (data)
        XFree(data);

    return 0;
}

int ewmh_get_window_desktop(AppContext *ctx, Window win)
{
    unsigned long nitems;
    unsigned long *data;

    data = (unsigned long *)ewmh_get_window_property(ctx, win,
                                                      ctx->atoms->net_wm_desktop,
                                                      XA_CARDINAL, &nitems);
    if (data && nitems > 0) {
        int desktop = (int)*data;
        XFree(data);
        return desktop;
    }

    if (data)
        XFree(data);

    /* Return -1 for sticky windows or windows without desktop property */
    return -1;
}

char *ewmh_get_window_name(AppContext *ctx, Window win)
{
    unsigned long nitems;
    unsigned char *data;
    char *name = NULL;

    /* Try _NET_WM_NAME (UTF-8) first */
    data = ewmh_get_window_property(ctx, win, ctx->atoms->net_wm_name,
                                    ctx->atoms->utf8_string, &nitems);
    if (data && nitems > 0) {
        name = strdup((char *)data);
        XFree(data);
        return name;
    }

    if (data)
        XFree(data);

    /* Fallback to WM_NAME (legacy, usually Latin-1) */
    data = ewmh_get_window_property(ctx, win, ctx->atoms->wm_name,
                                    XA_STRING, &nitems);
    if (data && nitems > 0) {
        name = strdup((char *)data);
        XFree(data);
        return name;
    }

    if (data)
        XFree(data);

    /* Try XFetchName as last resort */
    char *xname = NULL;
    if (XFetchName(ctx->display, win, &xname) && xname) {
        name = strdup(xname);
        XFree(xname);
        return name;
    }

    return NULL;
}

Atom ewmh_get_window_type(AppContext *ctx, Window win)
{
    unsigned long nitems;
    Atom *data;

    data = (Atom *)ewmh_get_window_property(ctx, win,
                                             ctx->atoms->net_wm_window_type,
                                             XA_ATOM, &nitems);
    if (data && nitems > 0) {
        Atom type = data[0];
        XFree(data);
        return type;
    }

    if (data)
        XFree(data);

    /* Default to normal window type */
    return ctx->atoms->net_wm_window_type_normal;
}

bool ewmh_is_window_hidden(AppContext *ctx, Window win)
{
    unsigned long nitems;
    Atom *states;

    states = (Atom *)ewmh_get_window_property(ctx, win,
                                               ctx->atoms->net_wm_state,
                                               XA_ATOM, &nitems);
    if (!states || nitems == 0) {
        if (states)
            XFree(states);
        return false;
    }

    bool hidden = false;
    for (unsigned long i = 0; i < nitems; i++) {
        if (states[i] == ctx->atoms->net_wm_state_hidden) {
            hidden = true;
            break;
        }
    }

    XFree(states);
    return hidden;
}

void ewmh_activate_window(AppContext *ctx, Window win)
{
    XEvent ev;

    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = win;
    ev.xclient.message_type = ctx->atoms->net_active_window;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 2;  /* Source indication: pager/task switcher */
    ev.xclient.data.l[1] = CurrentTime;
    ev.xclient.data.l[2] = 0;  /* Currently active window (0 = none) */

    XSendEvent(ctx->display, ctx->root, False,
               SubstructureNotifyMask | SubstructureRedirectMask, &ev);
    XFlush(ctx->display);
}
