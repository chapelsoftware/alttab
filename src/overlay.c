/*
 * alttab - Alt+Tab window switcher
 * overlay.c - Overlay window rendering
 */

/* For M_PI and strdup */
#define _GNU_SOURCE
#include <math.h>

#include "main.h"
#include "overlay.h"
#include "layout.h"

/* Default overlay dimensions */
#define OVERLAY_WIDTH  600
#define OVERLAY_HEIGHT 200

/* Tile rendering constants */
#define TILE_CORNER_RADIUS    8.0
#define TILE_BORDER_WIDTH     2.0
#define TILE_TEXT_HEIGHT      24    /* Height reserved for window title */
#define TILE_TEXT_PADDING     4     /* Padding around text */
#define TILE_FONT_SIZE        12.0
#define TILE_FONT_FAMILY      "Sans"

/* Colors (RGBA) */
#define OVERLAY_BG_R          0.1
#define OVERLAY_BG_G          0.1
#define OVERLAY_BG_B          0.1
#define OVERLAY_BG_A          0.85

#define TILE_BG_R             0.2
#define TILE_BG_G             0.2
#define TILE_BG_B             0.2
#define TILE_BG_A             0.9

#define TILE_SELECTED_R       0.3
#define TILE_SELECTED_G       0.5
#define TILE_SELECTED_B       0.8
#define TILE_SELECTED_A       0.9

#define TILE_BORDER_R         0.4
#define TILE_BORDER_G         0.6
#define TILE_BORDER_B         0.9
#define TILE_BORDER_A         1.0

#define TILE_TEXT_R           0.9
#define TILE_TEXT_G           0.9
#define TILE_TEXT_B           0.9
#define TILE_TEXT_A           1.0

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

/*
 * Helper: Draw a rounded rectangle path.
 */
static void draw_rounded_rect(cairo_t *cr, double x, double y,
                               double width, double height, double radius)
{
    /* Handle case where radius is too large */
    if (radius > width / 2.0)
        radius = width / 2.0;
    if (radius > height / 2.0)
        radius = height / 2.0;

    cairo_new_sub_path(cr);
    cairo_arc(cr, x + width - radius, y + radius, radius,
              -M_PI / 2.0, 0);
    cairo_arc(cr, x + width - radius, y + height - radius, radius,
              0, M_PI / 2.0);
    cairo_arc(cr, x + radius, y + height - radius, radius,
              M_PI / 2.0, M_PI);
    cairo_arc(cr, x + radius, y + radius, radius,
              M_PI, 3.0 * M_PI / 2.0);
    cairo_close_path(cr);
}

/*
 * Truncate text with ellipsis if it exceeds max_width.
 * Returns a newly allocated string that must be freed by caller.
 */
static char *truncate_text(cairo_t *cr, const char *text, double max_width)
{
    if (!text || max_width <= 0)
        return strdup("");

    cairo_text_extents_t extents;
    cairo_text_extents(cr, text, &extents);

    /* Text fits */
    if (extents.width <= max_width)
        return strdup(text);

    /* Need to truncate - binary search for fitting length */
    const char *ellipsis = "...";
    cairo_text_extents_t ellipsis_extents;
    cairo_text_extents(cr, ellipsis, &ellipsis_extents);

    double available = max_width - ellipsis_extents.width;
    if (available <= 0)
        return strdup(ellipsis);

    int len = strlen(text);
    char *result = malloc(len + 4);  /* +4 for "..." and null */
    if (!result)
        return strdup(ellipsis);

    /* Find how much of the original text fits */
    for (int i = len - 1; i >= 0; i--) {
        strncpy(result, text, i);
        result[i] = '\0';
        cairo_text_extents(cr, result, &extents);
        if (extents.width <= available) {
            strcpy(result + i, ellipsis);
            return result;
        }
    }

    free(result);
    return strdup(ellipsis);
}

/*
 * Clear the overlay to transparent, then fill with semi-transparent background.
 */
void overlay_clear(AppContext *ctx)
{
    OverlayState *state = &overlay_state;
    (void)ctx;

    if (!state->cr)
        return;

    /* Clear to fully transparent first */
    cairo_save(state->cr);
    cairo_set_operator(state->cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(state->cr, 0, 0, 0, 0);
    cairo_paint(state->cr);
    cairo_restore(state->cr);

    /* Draw semi-transparent background with rounded corners */
    cairo_set_source_rgba(state->cr, OVERLAY_BG_R, OVERLAY_BG_G,
                          OVERLAY_BG_B, OVERLAY_BG_A);
    draw_rounded_rect(state->cr, 0, 0, state->width, state->height,
                      TILE_CORNER_RADIUS * 2);
    cairo_fill(state->cr);
}

/* Icon rendering constants */
#define TILE_ICON_SIZE        32    /* Target icon size in pixels */
#define TILE_ICON_PADDING     6     /* Padding from tile edge */

/*
 * Draw a single tile at the specified position.
 */
void overlay_draw_tile(AppContext *ctx, int x, int y, int width, int height,
                       const char *title, bool selected, cairo_surface_t *icon)
{
    OverlayState *state = &overlay_state;
    (void)ctx;

    if (!state->cr)
        return;

    double dx = (double)x;
    double dy = (double)y;
    double dw = (double)width;
    double dh = (double)height;

    /* Draw tile background */
    if (selected) {
        cairo_set_source_rgba(state->cr, TILE_SELECTED_R, TILE_SELECTED_G,
                              TILE_SELECTED_B, TILE_SELECTED_A);
    } else {
        cairo_set_source_rgba(state->cr, TILE_BG_R, TILE_BG_G,
                              TILE_BG_B, TILE_BG_A);
    }
    draw_rounded_rect(state->cr, dx, dy, dw, dh, TILE_CORNER_RADIUS);
    cairo_fill(state->cr);

    /* Draw selection border if selected */
    if (selected) {
        cairo_set_source_rgba(state->cr, TILE_BORDER_R, TILE_BORDER_G,
                              TILE_BORDER_B, TILE_BORDER_A);
        cairo_set_line_width(state->cr, TILE_BORDER_WIDTH);
        draw_rounded_rect(state->cr, dx + TILE_BORDER_WIDTH / 2,
                          dy + TILE_BORDER_WIDTH / 2,
                          dw - TILE_BORDER_WIDTH,
                          dh - TILE_BORDER_WIDTH,
                          TILE_CORNER_RADIUS);
        cairo_stroke(state->cr);
    }

    /* Draw icon in bottom-left corner if available */
    if (icon && cairo_surface_status(icon) == CAIRO_STATUS_SUCCESS) {
        int icon_height = cairo_image_surface_get_height(icon);

        /* Position icon at bottom-left with padding, above the text area */
        double icon_x = dx + TILE_ICON_PADDING;
        double icon_y = dy + dh - TILE_TEXT_HEIGHT - TILE_ICON_PADDING - icon_height;

        /* Ensure icon stays within tile bounds */
        if (icon_y < dy + TILE_ICON_PADDING) {
            icon_y = dy + TILE_ICON_PADDING;
        }

        /* Draw the icon with Cairo scaling for smooth rendering */
        cairo_save(state->cr);
        cairo_set_source_surface(state->cr, icon, icon_x, icon_y);
        cairo_pattern_set_filter(cairo_get_source(state->cr), CAIRO_FILTER_BILINEAR);
        cairo_paint(state->cr);
        cairo_restore(state->cr);
    }

    /* Draw window title at bottom of tile */
    if (title && title[0]) {
        /* Set up font */
        cairo_select_font_face(state->cr, TILE_FONT_FAMILY,
                               CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(state->cr, TILE_FONT_SIZE);

        /* Truncate text to fit within tile */
        double text_max_width = dw - 2 * TILE_TEXT_PADDING;
        char *truncated = truncate_text(state->cr, title, text_max_width);

        /* Get text dimensions for centering */
        cairo_text_extents_t extents;
        cairo_text_extents(state->cr, truncated, &extents);

        /* Position text at bottom center of tile */
        double text_x = dx + (dw - extents.width) / 2.0 - extents.x_bearing;
        double text_y = dy + dh - TILE_TEXT_PADDING - extents.height / 2.0;

        /* Draw text with slight shadow for readability */
        cairo_set_source_rgba(state->cr, 0, 0, 0, 0.5);
        cairo_move_to(state->cr, text_x + 1, text_y + 1);
        cairo_show_text(state->cr, truncated);

        /* Draw actual text */
        cairo_set_source_rgba(state->cr, TILE_TEXT_R, TILE_TEXT_G,
                              TILE_TEXT_B, TILE_TEXT_A);
        cairo_move_to(state->cr, text_x, text_y);
        cairo_show_text(state->cr, truncated);

        free(truncated);
    }
}

/*
 * Flush the Cairo surface and X display.
 */
void overlay_flush(AppContext *ctx)
{
    OverlayState *state = &overlay_state;

    if (!state->surface)
        return;

    cairo_surface_flush(state->surface);
    XFlush(ctx->display);
}

/*
 * Redraw the entire overlay with all tiles.
 */
void redraw_overlay(AppContext *ctx)
{
    OverlayState *state = &overlay_state;

    if (!state->cr || !state->visible)
        return;

    if (!ctx->windows || ctx->windows->count == 0) {
        /* No windows - just show empty background */
        overlay_clear(ctx);
        overlay_flush(ctx);
        return;
    }

    /* Calculate layout */
    LayoutParams layout;
    int screen_width = DisplayWidth(ctx->display, ctx->screen);
    int screen_height = DisplayHeight(ctx->display, ctx->screen);
    layout_calculate(&layout, ctx->windows->count, screen_width, screen_height);

    /* Resize overlay if needed */
    if (state->width != layout.overlay_width ||
        state->height != layout.overlay_height) {
        state->width = layout.overlay_width;
        state->height = layout.overlay_height;

        /* Move and resize window */
        XMoveResizeWindow(ctx->display, state->window,
                          layout.overlay_x, layout.overlay_y,
                          layout.overlay_width, layout.overlay_height);

        /* Resize Cairo surface */
        cairo_xlib_surface_set_size(state->surface,
                                    layout.overlay_width,
                                    layout.overlay_height);
    }

    /* Clear overlay */
    overlay_clear(ctx);

    /* Draw each tile */
    for (int i = 0; i < ctx->windows->count; i++) {
        int tile_x, tile_y;
        layout_get_tile_position(&layout, i, &tile_x, &tile_y);

        bool selected = (i == ctx->selected_index);
        const char *title = ctx->windows->windows[i].name;
        if (!title)
            title = "Unknown";

        cairo_surface_t *icon = ctx->windows->windows[i].icon;
        overlay_draw_tile(ctx, tile_x, tile_y,
                          layout.tile_width, layout.tile_height,
                          title, selected, icon);
    }

    /* Flush to display */
    overlay_flush(ctx);
}

void overlay_draw(AppContext *ctx)
{
    /* overlay_draw is now a wrapper around redraw_overlay for compatibility */
    redraw_overlay(ctx);
}
