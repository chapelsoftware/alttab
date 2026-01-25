/*
 * alttab - Alt+Tab window switcher
 * thumbnail.h - Window thumbnail capture using XComposite
 */

#ifndef ALTTAB_THUMBNAIL_H
#define ALTTAB_THUMBNAIL_H

#include "main.h"

/*
 * Initialize thumbnail subsystem.
 * Checks for XComposite extension (version 0.2+) and XRender extension.
 * Returns true if compositor is available and ready.
 */
bool thumbnail_init(AppContext *ctx);

/*
 * Clean up thumbnail subsystem resources.
 */
void thumbnail_cleanup(AppContext *ctx);

/*
 * Check if thumbnail capture is available.
 * Returns true if a compositor is running and XComposite is functional.
 */
bool thumbnail_available(AppContext *ctx);

/*
 * Capture a window thumbnail at the specified size.
 * Uses XComposite to get the window pixmap and scales it.
 * Returns Cairo surface at target width/height, or NULL on failure.
 * The caller is responsible for freeing the returned surface.
 *
 * Note: Maintains aspect ratio - the surface may have transparent
 * borders if the window aspect ratio differs from width/height.
 */
cairo_surface_t *thumbnail_capture(AppContext *ctx, Window win, int width, int height);

/*
 * Free a thumbnail surface.
 */
void thumbnail_free(cairo_surface_t *surface);

#endif /* ALTTAB_THUMBNAIL_H */
