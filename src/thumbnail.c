/*
 * alttab - Alt+Tab window switcher
 * thumbnail.c - Window thumbnail capture
 */

#include "main.h"

cairo_surface_t *thumbnail_capture(AppContext *ctx, Window win, int width, int height)
{
    (void)ctx;
    (void)win;
    (void)width;
    (void)height;
    return NULL;
}

void thumbnail_free(cairo_surface_t *surface)
{
    if (surface) {
        cairo_surface_destroy(surface);
    }
}
