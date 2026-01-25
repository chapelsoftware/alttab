/*
 * alttab - Alt+Tab window switcher
 * overlay.h - Overlay window rendering
 */

#ifndef ALTTAB_OVERLAY_H
#define ALTTAB_OVERLAY_H

#include "main.h"

/* Overlay state - stored separately from AppContext */
typedef struct OverlayState {
    Window window;
    Visual *visual;
    Colormap colormap;
    cairo_surface_t *surface;
    cairo_t *cr;
    int width;
    int height;
    int depth;
    bool visible;
} OverlayState;

/* Get the overlay state (initialized by overlay_create) */
OverlayState *overlay_get_state(void);

/* Clear overlay with semi-transparent background */
void overlay_clear(AppContext *ctx);

/* Draw a single tile at the specified position */
void overlay_draw_tile(AppContext *ctx, int x, int y, int width, int height,
                       const char *title, bool selected, cairo_surface_t *icon,
                       cairo_surface_t *thumbnail, bool is_minimized);

/* Flush Cairo surface and X display */
void overlay_flush(AppContext *ctx);

/* Clear and redraw all tiles */
void redraw_overlay(AppContext *ctx);

#endif /* ALTTAB_OVERLAY_H */
