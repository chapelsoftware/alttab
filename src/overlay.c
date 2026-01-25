/*
 * alttab - Alt+Tab window switcher
 * overlay.c - Overlay window rendering
 */

/* For M_PI and strdup */
#define _GNU_SOURCE
#include <math.h>

#include "main.h"
#include "config.h"
#include "overlay.h"
#include "layout.h"
#include "util.h"
#include <X11/extensions/Xinerama.h>

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

/* Track whether we have true transparency (ARGB visual with compositor) */
static bool overlay_has_transparency = false;

/* Active monitor geometry */
static int monitor_x = 0;
static int monitor_y = 0;
static int monitor_width = 0;
static int monitor_height = 0;

/*
 * Get the geometry of the monitor containing the mouse pointer.
 * Falls back to full screen if Xinerama is not available.
 */
static void get_active_monitor_geometry(AppContext *ctx)
{
    /* Default to full screen */
    monitor_x = 0;
    monitor_y = 0;
    monitor_width = DisplayWidth(ctx->display, ctx->screen);
    monitor_height = DisplayHeight(ctx->display, ctx->screen);

    /* Check if Xinerama is available */
    int event_base, error_base;
    if (!XineramaQueryExtension(ctx->display, &event_base, &error_base)) {
        log_debug("Xinerama not available, using full screen");
        return;
    }

    if (!XineramaIsActive(ctx->display)) {
        log_debug("Xinerama not active, using full screen");
        return;
    }

    /* Get monitor info */
    int num_monitors;
    XineramaScreenInfo *monitors = XineramaQueryScreens(ctx->display, &num_monitors);
    if (!monitors || num_monitors == 0) {
        log_debug("No Xinerama screens found, using full screen");
        return;
    }

    /* Get mouse pointer position to determine active monitor */
    Window root_return, child_return;
    int root_x, root_y, win_x, win_y;
    unsigned int mask_return;

    if (XQueryPointer(ctx->display, ctx->root, &root_return, &child_return,
                      &root_x, &root_y, &win_x, &win_y, &mask_return)) {
        /* Find which monitor contains the pointer */
        for (int i = 0; i < num_monitors; i++) {
            XineramaScreenInfo *m = &monitors[i];
            if (root_x >= m->x_org && root_x < m->x_org + m->width &&
                root_y >= m->y_org && root_y < m->y_org + m->height) {
                monitor_x = m->x_org;
                monitor_y = m->y_org;
                monitor_width = m->width;
                monitor_height = m->height;
                log_debug("Active monitor %d: %dx%d+%d+%d",
                          i, monitor_width, monitor_height, monitor_x, monitor_y);
                break;
            }
        }
    }

    XFree(monitors);
}

OverlayState *overlay_get_state(void)
{
    return &overlay_state;
}

/*
 * Check if a compositor is running by looking for _NET_WM_CM_Sn atom.
 */
static bool compositor_running(AppContext *ctx)
{
    char atom_name[32];
    snprintf(atom_name, sizeof(atom_name), "_NET_WM_CM_S%d", ctx->screen);

    Atom cm_atom = XInternAtom(ctx->display, atom_name, False);
    Window cm_owner = XGetSelectionOwner(ctx->display, cm_atom);

    return (cm_owner != None);
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
    overlay_has_transparency = false;

    /* Check for compositor and 32-bit ARGB visual */
    bool have_compositor = compositor_running(ctx);
    if (have_compositor) {
        log_info("Compositor detected, attempting transparent overlay");
    } else {
        log_warning("No compositor running - using opaque overlay (thumbnails may not work)");
    }

    /* Try to find 32-bit ARGB visual for transparency */
    state->visual = find_argb_visual(ctx->display, ctx->screen, &state->depth);

    if (state->visual && have_compositor) {
        /* We have ARGB visual and compositor - use transparency */
        overlay_has_transparency = true;

        /* Create colormap for ARGB visual */
        state->colormap = XCreateColormap(ctx->display, ctx->root,
                                           state->visual, AllocNone);
        if (!state->colormap) {
            log_warning("Could not create ARGB colormap, falling back to opaque");
            overlay_has_transparency = false;
            state->visual = NULL;
        }
    }

    /* Fallback: use default visual (opaque window) */
    if (!state->visual || !overlay_has_transparency) {
        log_info("Using default visual (opaque mode)");
        state->visual = DefaultVisual(ctx->display, ctx->screen);
        state->depth = DefaultDepth(ctx->display, ctx->screen);
        state->colormap = DefaultColormap(ctx->display, ctx->screen);
        overlay_has_transparency = false;
    }

    /* Get active monitor geometry for centering */
    get_active_monitor_geometry(ctx);

    state->width = OVERLAY_WIDTH;
    state->height = OVERLAY_HEIGHT;

    int x = monitor_x + (monitor_width - state->width) / 2;
    int y = monitor_y + (monitor_height - state->height) / 2;

    /* Window attributes - override-redirect for popup behavior */
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;      /* No window decorations */
    attrs.colormap = state->colormap;
    attrs.border_pixel = 0;

    unsigned long attr_mask = CWOverrideRedirect | CWColormap | CWBorderPixel;

    if (overlay_has_transparency) {
        attrs.background_pixel = 0;      /* Transparent background */
        attr_mask |= CWBackPixel;
    } else {
        /* Opaque mode: set a dark background color from config */
        XColor bg_color;
        bg_color.red = (unsigned short)(g_config.bg_color.r * 65535);
        bg_color.green = (unsigned short)(g_config.bg_color.g * 65535);
        bg_color.blue = (unsigned short)(g_config.bg_color.b * 65535);
        bg_color.flags = DoRed | DoGreen | DoBlue;

        if (XAllocColor(ctx->display, state->colormap, &bg_color)) {
            attrs.background_pixel = bg_color.pixel;
            attr_mask |= CWBackPixel;
        }
    }

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
        if (overlay_has_transparency && state->colormap != DefaultColormap(ctx->display, ctx->screen)) {
            XFreeColormap(ctx->display, state->colormap);
        }
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
        if (overlay_has_transparency && state->colormap != DefaultColormap(ctx->display, ctx->screen)) {
            XFreeColormap(ctx->display, state->colormap);
        }
        return false;
    }

    /* Create Cairo context */
    state->cr = cairo_create(state->surface);
    if (cairo_status(state->cr) != CAIRO_STATUS_SUCCESS) {
        log_error("Could not create Cairo context");
        cairo_surface_destroy(state->surface);
        XDestroyWindow(ctx->display, state->window);
        if (overlay_has_transparency && state->colormap != DefaultColormap(ctx->display, ctx->screen)) {
            XFreeColormap(ctx->display, state->colormap);
        }
        return false;
    }

    state->visible = false;
    log_debug("Overlay window created: 0x%lx (depth=%d, %s)",
              state->window, state->depth,
              overlay_has_transparency ? "transparent" : "opaque");

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

    /* Only free colormap if we allocated it (transparency mode) */
    if (overlay_has_transparency && state->colormap &&
        state->colormap != DefaultColormap(ctx->display, ctx->screen)) {
        XFreeColormap(ctx->display, state->colormap);
    }
    state->colormap = None;

    state->visible = false;
    overlay_has_transparency = false;
    log_debug("Overlay window destroyed");
}

void overlay_show(AppContext *ctx)
{
    OverlayState *state = &overlay_state;

    if (!state->window)
        return;

    if (!state->visible) {
        /* Update active monitor geometry before showing */
        get_active_monitor_geometry(ctx);

        XMapRaised(ctx->display, state->window);
        state->visible = true;
        XFlush(ctx->display);
        log_debug("Overlay shown on monitor at %d,%d (%dx%d)",
                  monitor_x, monitor_y, monitor_width, monitor_height);
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
 * Clear the overlay to transparent (or opaque background if no compositor),
 * then fill with semi-transparent background.
 */
void overlay_clear(AppContext *ctx)
{
    OverlayState *state = &overlay_state;
    (void)ctx;

    if (!state->cr)
        return;

    /* Use configured background color */
    ColorRGBA bg = g_config.bg_color;

    if (overlay_has_transparency) {
        /* Clear to fully transparent first */
        cairo_save(state->cr);
        cairo_set_operator(state->cr, CAIRO_OPERATOR_SOURCE);
        cairo_set_source_rgba(state->cr, 0, 0, 0, 0);
        cairo_paint(state->cr);
        cairo_restore(state->cr);

        /* Draw semi-transparent background with rounded corners */
        cairo_set_source_rgba(state->cr, bg.r, bg.g, bg.b, bg.a);
        draw_rounded_rect(state->cr, 0, 0, state->width, state->height,
                          TILE_CORNER_RADIUS * 2);
        cairo_fill(state->cr);
    } else {
        /* Opaque mode: fill entire background with solid dark color */
        cairo_set_source_rgb(state->cr, bg.r, bg.g, bg.b);
        cairo_paint(state->cr);

        /* Draw subtle rounded rectangle border for visual separation */
        cairo_set_source_rgb(state->cr, 0.3, 0.3, 0.3);
        cairo_set_line_width(state->cr, 2.0);
        draw_rounded_rect(state->cr, 1, 1, state->width - 2, state->height - 2,
                          TILE_CORNER_RADIUS * 2);
        cairo_stroke(state->cr);
    }
}

/* Icon rendering constants */
#define TILE_ICON_SIZE        32    /* Target icon size in pixels */
#define TILE_ICON_PADDING     6     /* Padding from tile edge */

/* Thumbnail rendering constants */
#define THUMB_TILE_PADDING    8     /* Internal padding for thumbnail area */
#define THUMB_SHADOW_OFFSET   2     /* Shadow offset in pixels */
#define THUMB_BORDER_WIDTH    1.0   /* Subtle border width */

/* Minimized window placeholder colors */
#define MINIMIZED_BG_R        0.15
#define MINIMIZED_BG_G        0.15
#define MINIMIZED_BG_B        0.15
#define MINIMIZED_BG_A        0.6

/*
 * Draw a thumbnail or placeholder for minimized windows.
 * The thumbnail area is in the upper portion of the tile.
 */
static void draw_thumbnail_area(cairo_t *cr, double dx, double dy, double dw, double dh,
                                 cairo_surface_t *thumbnail, bool is_minimized)
{
    /* Calculate thumbnail area - upper portion of tile, leaving room for icon/title */
    double thumb_area_y = dy + THUMB_TILE_PADDING;
    double thumb_area_height = dh - TILE_TEXT_HEIGHT - TILE_TEXT_PADDING
                                - THUMB_TILE_PADDING * 2;
    double thumb_area_width = dw - THUMB_TILE_PADDING * 2;
    double thumb_area_x = dx + THUMB_TILE_PADDING;

    /* Ensure minimum dimensions */
    if (thumb_area_width < 40 || thumb_area_height < 30)
        return;

    if (thumbnail && cairo_surface_status(thumbnail) == CAIRO_STATUS_SUCCESS) {
        /* Get actual thumbnail dimensions */
        int thumb_width = cairo_image_surface_get_width(thumbnail);
        int thumb_height = cairo_image_surface_get_height(thumbnail);

        /* Center the thumbnail in the available area */
        double thumb_x = thumb_area_x + (thumb_area_width - thumb_width) / 2.0;
        double thumb_y = thumb_area_y + (thumb_area_height - thumb_height) / 2.0;

        /* Draw subtle shadow behind thumbnail */
        cairo_save(cr);
        cairo_set_source_rgba(cr, 0, 0, 0, 0.3);
        cairo_rectangle(cr, thumb_x + THUMB_SHADOW_OFFSET,
                        thumb_y + THUMB_SHADOW_OFFSET,
                        thumb_width, thumb_height);
        cairo_fill(cr);
        cairo_restore(cr);

        /* Draw the thumbnail */
        cairo_save(cr);
        cairo_set_source_surface(cr, thumbnail, thumb_x, thumb_y);
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
        cairo_paint(cr);
        cairo_restore(cr);

        /* Draw subtle border around thumbnail */
        cairo_save(cr);
        cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.5);
        cairo_set_line_width(cr, THUMB_BORDER_WIDTH);
        cairo_rectangle(cr, thumb_x, thumb_y, thumb_width, thumb_height);
        cairo_stroke(cr);
        cairo_restore(cr);
    } else if (is_minimized) {
        /* Draw placeholder for minimized windows */
        cairo_save(cr);

        /* Dark placeholder background */
        cairo_set_source_rgba(cr, MINIMIZED_BG_R, MINIMIZED_BG_G,
                              MINIMIZED_BG_B, MINIMIZED_BG_A);
        cairo_rectangle(cr, thumb_area_x, thumb_area_y,
                        thumb_area_width, thumb_area_height);
        cairo_fill(cr);

        /* Draw "Minimized" text indicator */
        cairo_select_font_face(cr, TILE_FONT_FAMILY,
                               CAIRO_FONT_SLANT_ITALIC,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, TILE_FONT_SIZE * 0.9);

        const char *label = "Minimized";
        cairo_text_extents_t extents;
        cairo_text_extents(cr, label, &extents);

        double label_x = thumb_area_x + (thumb_area_width - extents.width) / 2.0
                         - extents.x_bearing;
        double label_y = thumb_area_y + (thumb_area_height + extents.height) / 2.0;

        cairo_set_source_rgba(cr, 0.6, 0.6, 0.6, 0.8);
        cairo_move_to(cr, label_x, label_y);
        cairo_show_text(cr, label);

        cairo_restore(cr);
    }
}

/*
 * Draw a single tile at the specified position.
 */
void overlay_draw_tile(AppContext *ctx, int x, int y, int width, int height,
                       const char *title, bool selected, cairo_surface_t *icon,
                       cairo_surface_t *thumbnail, bool is_minimized)
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
        /* Use configured highlight color */
        ColorRGBA hl = g_config.highlight_color;
        cairo_set_source_rgba(state->cr, hl.r, hl.g, hl.b, hl.a);
    } else {
        cairo_set_source_rgba(state->cr, TILE_BG_R, TILE_BG_G,
                              TILE_BG_B, TILE_BG_A);
    }
    draw_rounded_rect(state->cr, dx, dy, dw, dh, TILE_CORNER_RADIUS);
    cairo_fill(state->cr);

    /* Draw selection border if selected */
    if (selected) {
        /* Use a brighter version of highlight color for border */
        ColorRGBA hl = g_config.highlight_color;
        cairo_set_source_rgba(state->cr,
                              hl.r + (1.0 - hl.r) * 0.3,
                              hl.g + (1.0 - hl.g) * 0.3,
                              hl.b + (1.0 - hl.b) * 0.3,
                              1.0);
        cairo_set_line_width(state->cr, TILE_BORDER_WIDTH);
        draw_rounded_rect(state->cr, dx + TILE_BORDER_WIDTH / 2,
                          dy + TILE_BORDER_WIDTH / 2,
                          dw - TILE_BORDER_WIDTH,
                          dh - TILE_BORDER_WIDTH,
                          TILE_CORNER_RADIUS);
        cairo_stroke(state->cr);
    }

    /* Draw thumbnail or placeholder in upper area of tile */
    draw_thumbnail_area(state->cr, dx, dy, dw, dh, thumbnail, is_minimized);

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

    /* Calculate layout using active monitor dimensions */
    LayoutParams layout;
    layout_calculate(&layout, ctx->windows->count, monitor_width, monitor_height);

    /* Resize overlay if needed */
    if (state->width != layout.overlay_width ||
        state->height != layout.overlay_height) {
        state->width = layout.overlay_width;
        state->height = layout.overlay_height;

        /* Move and resize window - position relative to active monitor */
        XMoveResizeWindow(ctx->display, state->window,
                          monitor_x + layout.overlay_x,
                          monitor_y + layout.overlay_y,
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
        cairo_surface_t *thumbnail = ctx->windows->windows[i].thumbnail;
        bool is_minimized = ctx->windows->windows[i].hidden;

        overlay_draw_tile(ctx, tile_x, tile_y,
                          layout.tile_width, layout.tile_height,
                          title, selected, icon, thumbnail, is_minimized);
    }

    /* Flush to display */
    overlay_flush(ctx);
}

void overlay_draw(AppContext *ctx)
{
    /* overlay_draw is now a wrapper around redraw_overlay for compatibility */
    redraw_overlay(ctx);
}
