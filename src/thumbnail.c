/*
 * alttab - Alt+Tab window switcher
 * thumbnail.c - Window thumbnail capture using XComposite
 *
 * Requires a running compositor (e.g., picom) for XComposite to work.
 * Uses XRender for hardware-accelerated scaling.
 */

#include "main.h"
#include <X11/extensions/Xrender.h>

/* Module state for XComposite/XRender */
static struct {
    bool initialized;
    bool available;
    int composite_event_base;
    int composite_error_base;
    int render_event_base;
    int render_error_base;
} thumbnail_state;

/*
 * Error handler to catch BadMatch/BadWindow errors during thumbnail capture.
 * These can occur when windows are destroyed during capture.
 */
static int capture_error_occurred = 0;

static int thumbnail_error_handler(Display *display, XErrorEvent *event)
{
    (void)display;

    /* Ignore BadMatch, BadWindow, and BadDrawable errors during capture */
    if (event->error_code == BadMatch ||
        event->error_code == BadWindow ||
        event->error_code == BadDrawable ||
        event->error_code == BadPixmap) {
        capture_error_occurred = 1;
        return 0;
    }

    /* Log other errors */
    char error_text[256];
    XGetErrorText(display, event->error_code, error_text, sizeof(error_text));
    log_warning("X error in thumbnail: %s (code=%d)", error_text, event->error_code);
    capture_error_occurred = 1;
    return 0;
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

bool thumbnail_init(AppContext *ctx)
{
    if (thumbnail_state.initialized)
        return thumbnail_state.available;

    thumbnail_state.initialized = true;
    thumbnail_state.available = false;

    /* Check XComposite extension (need version 0.2+ for NameWindowPixmap) */
    if (!XCompositeQueryExtension(ctx->display,
                                   &thumbnail_state.composite_event_base,
                                   &thumbnail_state.composite_error_base)) {
        log_warning("XComposite extension not available");
        return false;
    }

    int major = 0, minor = 0;
    XCompositeQueryVersion(ctx->display, &major, &minor);

    if (major < 0 || (major == 0 && minor < 2)) {
        log_warning("XComposite version %d.%d too old (need 0.2+)", major, minor);
        return false;
    }

    log_debug("XComposite version %d.%d available", major, minor);

    /* Check XRender extension (for scaled rendering) */
    if (!XRenderQueryExtension(ctx->display,
                                &thumbnail_state.render_event_base,
                                &thumbnail_state.render_error_base)) {
        log_warning("XRender extension not available");
        return false;
    }

    log_debug("XRender extension available");

    /* Check if a compositor is running */
    if (!compositor_running(ctx)) {
        log_warning("No compositor running (picom required for thumbnails)");
        /* Don't fail here - compositor might start later */
    } else {
        log_debug("Compositor detected");
    }

    thumbnail_state.available = true;
    return true;
}

void thumbnail_cleanup(AppContext *ctx)
{
    (void)ctx;
    /* Nothing to clean up currently - state is static */
    thumbnail_state.initialized = false;
    thumbnail_state.available = false;
}

bool thumbnail_available(AppContext *ctx)
{
    if (!thumbnail_state.initialized) {
        thumbnail_init(ctx);
    }

    /* Re-check compositor on each call since it might have started/stopped */
    return thumbnail_state.available && compositor_running(ctx);
}

/*
 * Get window attributes with error handling.
 * Returns true on success.
 */
static bool get_window_attrs(AppContext *ctx, Window win,
                              XWindowAttributes *attrs)
{
    XErrorHandler old_handler = XSetErrorHandler(thumbnail_error_handler);
    capture_error_occurred = 0;

    Status status = XGetWindowAttributes(ctx->display, win, attrs);
    XSync(ctx->display, False);

    XSetErrorHandler(old_handler);

    return (status && !capture_error_occurred);
}

/*
 * Redirect a window for compositing and get its pixmap.
 * Returns the pixmap on success, None on failure.
 */
static Pixmap get_window_pixmap(AppContext *ctx, Window win)
{
    XErrorHandler old_handler = XSetErrorHandler(thumbnail_error_handler);
    capture_error_occurred = 0;

    /* Redirect window for compositing (automatic mode - let compositor manage) */
    XCompositeRedirectWindow(ctx->display, win, CompositeRedirectAutomatic);
    XSync(ctx->display, False);

    if (capture_error_occurred) {
        XSetErrorHandler(old_handler);
        return None;
    }

    /* Get the window's offscreen pixmap */
    Pixmap pixmap = XCompositeNameWindowPixmap(ctx->display, win);
    XSync(ctx->display, False);

    if (capture_error_occurred) {
        XCompositeUnredirectWindow(ctx->display, win, CompositeRedirectAutomatic);
        XSetErrorHandler(old_handler);
        return None;
    }

    XSetErrorHandler(old_handler);
    return pixmap;
}

/*
 * Find an appropriate XRender picture format for a visual.
 */
static XRenderPictFormat *get_picture_format(AppContext *ctx, Visual *visual)
{
    return XRenderFindVisualFormat(ctx->display, visual);
}

/*
 * Scale a source pixmap to target dimensions using XRender.
 * Returns a new Cairo surface, or NULL on failure.
 */
static cairo_surface_t *scale_pixmap_xrender(AppContext *ctx,
                                              Pixmap src_pixmap,
                                              Visual *src_visual,
                                              int src_width, int src_height,
                                              int target_width, int target_height)
{
    /* Calculate scaled dimensions maintaining aspect ratio */
    double scale_x = (double)target_width / src_width;
    double scale_y = (double)target_height / src_height;
    double scale = (scale_x < scale_y) ? scale_x : scale_y;

    int scaled_width = (int)(src_width * scale);
    int scaled_height = (int)(src_height * scale);

    /* Center offset in target area */
    int offset_x = (target_width - scaled_width) / 2;
    int offset_y = (target_height - scaled_height) / 2;

    /* Get picture format for source */
    XRenderPictFormat *src_format = get_picture_format(ctx, src_visual);
    if (!src_format) {
        log_warning("Could not find XRender format for visual");
        return NULL;
    }

    /* Create Picture for source pixmap */
    XRenderPictureAttributes src_attrs;
    src_attrs.subwindow_mode = IncludeInferiors;

    Picture src_picture = XRenderCreatePicture(ctx->display, src_pixmap,
                                                src_format, CPSubwindowMode,
                                                &src_attrs);
    if (!src_picture) {
        log_warning("Could not create source XRender Picture");
        return NULL;
    }

    /* Set bilinear filter for quality scaling */
    XRenderSetPictureFilter(ctx->display, src_picture, FilterBilinear, NULL, 0);

    /* Create target Cairo surface (ARGB32 for transparency) */
    cairo_surface_t *surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, target_width, target_height);

    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        XRenderFreePicture(ctx->display, src_picture);
        return NULL;
    }

    /* Create an X pixmap to render into, then copy to Cairo */
    Pixmap dest_pixmap = XCreatePixmap(ctx->display, ctx->root,
                                        target_width, target_height, 32);
    if (!dest_pixmap) {
        XRenderFreePicture(ctx->display, src_picture);
        cairo_surface_destroy(surface);
        return NULL;
    }

    /* Find 32-bit ARGB format for destination */
    XRenderPictFormat *dest_format = XRenderFindStandardFormat(ctx->display,
                                                                 PictStandardARGB32);
    if (!dest_format) {
        XFreePixmap(ctx->display, dest_pixmap);
        XRenderFreePicture(ctx->display, src_picture);
        cairo_surface_destroy(surface);
        return NULL;
    }

    Picture dest_picture = XRenderCreatePicture(ctx->display, dest_pixmap,
                                                  dest_format, 0, NULL);
    if (!dest_picture) {
        XFreePixmap(ctx->display, dest_pixmap);
        XRenderFreePicture(ctx->display, src_picture);
        cairo_surface_destroy(surface);
        return NULL;
    }

    /* Clear destination to transparent */
    XRenderColor transparent = {0, 0, 0, 0};
    XRenderFillRectangle(ctx->display, PictOpSrc, dest_picture,
                          &transparent, 0, 0, target_width, target_height);

    /* Set up transformation matrix for scaling */
    XTransform xform = {{
        {XDoubleToFixed(1.0 / scale), XDoubleToFixed(0), XDoubleToFixed(0)},
        {XDoubleToFixed(0), XDoubleToFixed(1.0 / scale), XDoubleToFixed(0)},
        {XDoubleToFixed(0), XDoubleToFixed(0), XDoubleToFixed(1.0)}
    }};

    XRenderSetPictureTransform(ctx->display, src_picture, &xform);

    /* Composite scaled source onto destination */
    XRenderComposite(ctx->display, PictOpSrc,
                      src_picture, None, dest_picture,
                      0, 0,           /* src x, y */
                      0, 0,           /* mask x, y */
                      offset_x, offset_y, /* dest x, y */
                      scaled_width, scaled_height);

    XSync(ctx->display, False);

    /* Copy from X pixmap to Cairo surface */
    /* We need to read the pixels back since we used X rendering */
    XImage *image = XGetImage(ctx->display, dest_pixmap,
                               0, 0, target_width, target_height,
                               AllPlanes, ZPixmap);

    if (image) {
        unsigned char *dest = cairo_image_surface_get_data(surface);
        int stride = cairo_image_surface_get_stride(surface);

        cairo_surface_flush(surface);

        for (int y = 0; y < target_height; y++) {
            for (int x = 0; x < target_width; x++) {
                unsigned long pixel = XGetPixel(image, x, y);
                /* ARGB pixel format */
                unsigned char *p = dest + y * stride + x * 4;
                p[0] = (pixel >> 0) & 0xFF;   /* B */
                p[1] = (pixel >> 8) & 0xFF;   /* G */
                p[2] = (pixel >> 16) & 0xFF;  /* R */
                p[3] = (pixel >> 24) & 0xFF;  /* A */
            }
        }

        cairo_surface_mark_dirty(surface);
        XDestroyImage(image);
    }

    /* Clean up X resources */
    XRenderFreePicture(ctx->display, dest_picture);
    XRenderFreePicture(ctx->display, src_picture);
    XFreePixmap(ctx->display, dest_pixmap);

    return surface;
}

/*
 * Fallback: Scale pixmap using Cairo (slower but always works).
 */
static cairo_surface_t *scale_pixmap_cairo(AppContext *ctx,
                                            Pixmap src_pixmap,
                                            Visual *src_visual,
                                            int depth,
                                            int src_width, int src_height,
                                            int target_width, int target_height)
{
    /* Create Cairo surface from X pixmap */
    cairo_surface_t *src_surface = cairo_xlib_surface_create(
        ctx->display, src_pixmap, src_visual, src_width, src_height);

    if (cairo_surface_status(src_surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(src_surface);
        return NULL;
    }

    /* Create target surface */
    cairo_surface_t *target = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, target_width, target_height);

    if (cairo_surface_status(target) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(src_surface);
        cairo_surface_destroy(target);
        return NULL;
    }

    cairo_t *cr = cairo_create(target);

    /* Calculate scale maintaining aspect ratio */
    double scale_x = (double)target_width / src_width;
    double scale_y = (double)target_height / src_height;
    double scale = (scale_x < scale_y) ? scale_x : scale_y;

    /* Center the thumbnail */
    double offset_x = (target_width - src_width * scale) / 2.0;
    double offset_y = (target_height - src_height * scale) / 2.0;

    /* Clear to transparent */
    cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(cr, 0, 0, 0, 0);
    cairo_paint(cr);

    /* Scale and draw */
    cairo_translate(cr, offset_x, offset_y);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, src_surface, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
    cairo_paint(cr);

    cairo_destroy(cr);
    cairo_surface_destroy(src_surface);

    /* Mark unused parameter */
    (void)depth;

    return target;
}

cairo_surface_t *thumbnail_capture(AppContext *ctx, Window win, int width, int height)
{
    if (!ctx || win == None || width <= 0 || height <= 0)
        return NULL;

    /* Ensure subsystem is initialized */
    if (!thumbnail_state.initialized) {
        thumbnail_init(ctx);
    }

    /* Check if compositor is available */
    if (!thumbnail_state.available) {
        return NULL;
    }

    /* Check if compositor is running (can change at runtime) */
    if (!compositor_running(ctx)) {
        return NULL;
    }

    /* Get window attributes */
    XWindowAttributes attrs;
    if (!get_window_attrs(ctx, win, &attrs)) {
        log_debug("Could not get window attributes for 0x%lx", win);
        return NULL;
    }

    /* Skip if window is not viewable (unmapped windows don't have pixmaps) */
    if (attrs.map_state != IsViewable) {
        return NULL;
    }

    /* Get window's composited pixmap */
    Pixmap pixmap = get_window_pixmap(ctx, win);
    if (pixmap == None) {
        log_debug("Could not get pixmap for window 0x%lx", win);
        return NULL;
    }

    /* Try XRender scaling first (hardware-accelerated) */
    cairo_surface_t *result = scale_pixmap_xrender(ctx, pixmap, attrs.visual,
                                                    attrs.width, attrs.height,
                                                    width, height);

    /* Fall back to Cairo scaling if XRender fails */
    if (!result) {
        result = scale_pixmap_cairo(ctx, pixmap, attrs.visual, attrs.depth,
                                     attrs.width, attrs.height,
                                     width, height);
    }

    /* Unredirect window and free pixmap */
    XErrorHandler old_handler = XSetErrorHandler(thumbnail_error_handler);
    capture_error_occurred = 0;

    XFreePixmap(ctx->display, pixmap);
    XCompositeUnredirectWindow(ctx->display, win, CompositeRedirectAutomatic);
    XSync(ctx->display, False);

    XSetErrorHandler(old_handler);

    return result;
}

void thumbnail_free(cairo_surface_t *surface)
{
    if (surface) {
        cairo_surface_destroy(surface);
    }
}
