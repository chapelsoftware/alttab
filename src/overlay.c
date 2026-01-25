/*
 * alttab - Alt+Tab window switcher
 * overlay.c - Overlay window rendering
 */

#include "main.h"
#include "overlay.h"

/* Default overlay dimensions */
#define OVERLAY_WIDTH  600
#define OVERLAY_HEIGHT 200

/* Static overlay state */
static OverlayState overlay_state;

OverlayState *overlay_get_state(void)
{
    return &overlay_state;
}

/*
 * Find a 32-bit ARGB visual for transparent windows.
 * Returns the visual on success, NULL if not found.
 */
static Visual *find_argb_visual(Display *display, int screen, int *depth_out)
{
    XVisualInfo vinfo_template;
    XVisualInfo *vinfo_list;
    int num_visuals;
    Visual *result = NULL;

    vinfo_template.screen = screen;
    vinfo_template.depth = 32;
    vinfo_template.class = TrueColor;

    vinfo_list = XGetVisualInfo(display,
                                 VisualScreenMask | VisualDepthMask | VisualClassMask,
                                 &vinfo_template, &num_visuals);

    if (vinfo_list && num_visuals > 0) {
        /* Find a visual with alpha channel (32-bit ARGB) */
        for (int i = 0; i < num_visuals; i++) {
            XRenderPictFormat *fmt = XRenderFindVisualFormat(display,
                                                              vinfo_list[i].visual);
            if (fmt && fmt->type == PictTypeDirect && fmt->direct.alphaMask) {
                result = vinfo_list[i].visual;
                *depth_out = vinfo_list[i].depth;
                break;
            }
        }
    }

    if (vinfo_list)
        XFree(vinfo_list);

    return result;
}

/*
 * Set EWMH properties on the overlay window.
 */
static void set_overlay_properties(AppContext *ctx, Window win)
{
    /* Set window type to dialog (floats above normal windows) */
    XChangeProperty(ctx->display, win,
                    ctx->atoms->net_wm_window_type,
                    XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)&ctx->atoms->net_wm_window_type_dialog, 1);

    /* Set _NET_WM_STATE: above, skip taskbar, skip pager */
    Atom states[3] = {
        ctx->atoms->net_wm_state_above,
        ctx->atoms->net_wm_state_skip_taskbar,
        ctx->atoms->net_wm_state_skip_pager
    };
    XChangeProperty(ctx->display, win,
                    ctx->atoms->net_wm_state,
                    XA_ATOM, 32, PropModeReplace,
                    (unsigned char *)states, 3);

    /* Set window name */
    const char *name = "alttab";
    XChangeProperty(ctx->display, win,
                    ctx->atoms->net_wm_name,
                    ctx->atoms->utf8_string, 8, PropModeReplace,
                    (unsigned char *)name, strlen(name));
}

bool overlay_create(AppContext *ctx)
{
    OverlayState *state = &overlay_state;
    memset(state, 0, sizeof(*state));

    /* Find 32-bit ARGB visual */
    state->visual = find_argb_visual(ctx->display, ctx->screen, &state->depth);
    if (!state->visual) {
        log_error("Could not find 32-bit ARGB visual");
        return false;
    }

    /* Create colormap for ARGB visual */
    state->colormap = XCreateColormap(ctx->display, ctx->root,
                                       state->visual, AllocNone);
    if (!state->colormap) {
        log_error("Could not create colormap");
        return false;
    }

    /* Get screen dimensions for centering */
    int screen_width = DisplayWidth(ctx->display, ctx->screen);
    int screen_height = DisplayHeight(ctx->display, ctx->screen);

    state->width = OVERLAY_WIDTH;
    state->height = OVERLAY_HEIGHT;

    int x = (screen_width - state->width) / 2;
    int y = (screen_height - state->height) / 2;

    /* Window attributes for transparent, override-redirect window */
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;      /* No window decorations */
    attrs.colormap = state->colormap;
    attrs.border_pixel = 0;
    attrs.background_pixel = 0;          /* Transparent background */

    unsigned long attr_mask = CWOverrideRedirect | CWColormap |
                               CWBorderPixel | CWBackPixel;

    /* Create the window */
    state->window = XCreateWindow(ctx->display, ctx->root,
                                   x, y, state->width, state->height,
                                   0,                /* border width */
                                   state->depth,
                                   InputOutput,
                                   state->visual,
                                   attr_mask, &attrs);

    if (!state->window) {
        log_error("Could not create overlay window");
        XFreeColormap(ctx->display, state->colormap);
        return false;
    }

    /* Set EWMH properties */
    set_overlay_properties(ctx, state->window);

    /* Select input events for the window */
    XSelectInput(ctx->display, state->window,
                 ExposureMask | KeyPressMask | KeyReleaseMask);

    /* Create Cairo surface attached to the window */
    state->surface = cairo_xlib_surface_create(ctx->display, state->window,
                                                state->visual,
                                                state->width, state->height);
    if (cairo_surface_status(state->surface) != CAIRO_STATUS_SUCCESS) {
        log_error("Could not create Cairo surface");
        XDestroyWindow(ctx->display, state->window);
        XFreeColormap(ctx->display, state->colormap);
        return false;
    }

    /* Create Cairo context */
    state->cr = cairo_create(state->surface);
    if (cairo_status(state->cr) != CAIRO_STATUS_SUCCESS) {
        log_error("Could not create Cairo context");
        cairo_surface_destroy(state->surface);
        XDestroyWindow(ctx->display, state->window);
        XFreeColormap(ctx->display, state->colormap);
        return false;
    }

    state->visible = false;
    log_debug("Overlay window created: 0x%lx (depth=%d)", state->window, state->depth);

    return true;
}

void overlay_destroy(AppContext *ctx)
{
    OverlayState *state = &overlay_state;

    if (state->cr) {
        cairo_destroy(state->cr);
        state->cr = NULL;
    }

    if (state->surface) {
        cairo_surface_destroy(state->surface);
        state->surface = NULL;
    }

    if (state->window) {
        XDestroyWindow(ctx->display, state->window);
        state->window = None;
    }

    if (state->colormap) {
        XFreeColormap(ctx->display, state->colormap);
        state->colormap = None;
    }

    state->visible = false;
    log_debug("Overlay window destroyed");
}

void overlay_show(AppContext *ctx)
{
    OverlayState *state = &overlay_state;

    if (!state->window)
        return;

    if (!state->visible) {
        XMapRaised(ctx->display, state->window);
        state->visible = true;
        XFlush(ctx->display);
        log_debug("Overlay shown");
    }
}

void overlay_hide(AppContext *ctx)
{
    OverlayState *state = &overlay_state;

    if (!state->window)
        return;

    if (state->visible) {
        XUnmapWindow(ctx->display, state->window);
        state->visible = false;
        XFlush(ctx->display);
        log_debug("Overlay hidden");
    }
}

void overlay_draw(AppContext *ctx)
{
    OverlayState *state = &overlay_state;
    (void)ctx;

    if (!state->cr || !state->visible)
        return;

    /* Clear to transparent */
    cairo_save(state->cr);
    cairo_set_operator(state->cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(state->cr, 0, 0, 0, 0);
    cairo_paint(state->cr);
    cairo_restore(state->cr);

    /* Draw semi-transparent background */
    cairo_set_source_rgba(state->cr, 0.1, 0.1, 0.1, 0.85);
    cairo_rectangle(state->cr, 0, 0, state->width, state->height);
    cairo_fill(state->cr);

    /* Flush surface to window */
    cairo_surface_flush(state->surface);
    XFlush(ctx->display);
}
