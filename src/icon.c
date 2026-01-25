/*
 * alttab - Alt+Tab window switcher
 * icon.c - Window icon retrieval
 */

#include "main.h"

cairo_surface_t *icon_get(AppContext *ctx, Window win, int size)
{
    (void)ctx;
    (void)win;
    (void)size;
    return NULL;
}

void icon_free(cairo_surface_t *surface)
{
    if (surface) {
        cairo_surface_destroy(surface);
    }
}
