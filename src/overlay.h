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

#endif /* ALTTAB_OVERLAY_H */
