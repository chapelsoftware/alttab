/*
 * alttab - Alt+Tab window switcher
 * ewmh.c - EWMH protocol helpers
 */

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

Window *ewmh_get_client_list(AppContext *ctx, int *count)
{
    (void)ctx;
    *count = 0;
    return NULL;
}

int ewmh_get_current_desktop(AppContext *ctx)
{
    (void)ctx;
    return 0;
}

int ewmh_get_window_desktop(AppContext *ctx, Window win)
{
    (void)ctx;
    (void)win;
    return 0;
}

char *ewmh_get_window_name(AppContext *ctx, Window win)
{
    (void)ctx;
    (void)win;
    return NULL;
}

bool ewmh_is_window_hidden(AppContext *ctx, Window win)
{
    (void)ctx;
    (void)win;
    return false;
}

void ewmh_activate_window(AppContext *ctx, Window win)
{
    (void)ctx;
    (void)win;
}
