/*
 * alttab - Alt+Tab window switcher
 * icon.c - Window icon retrieval
 */

#include <stdint.h>
#include "main.h"
#include "util.h"

/*
 * _NET_WM_ICON format:
 * The property contains an array of 32-bit values representing one or more icons.
 * Each icon starts with: width (32-bit), height (32-bit), then width*height ARGB pixels.
 * Multiple icons are concatenated directly.
 *
 * ARGB format: 32-bit value where byte order is A, R, G, B (most significant to least).
 * Cairo expects ARGB32 in native byte order with premultiplied alpha.
 */

/*
 * Structure to hold icon data from _NET_WM_ICON.
 */
typedef struct {
    int width;
    int height;
    unsigned long *data;  /* Points into the property data (not owned) */
} IconEntry;

/*
 * Parse _NET_WM_ICON property data to find all icons.
 * Returns array of IconEntry structs (caller must free), sets *count.
 * property_data and nitems are from XGetWindowProperty.
 */
static IconEntry *icon_parse_property(unsigned long *property_data,
                                       unsigned long nitems,
                                       int *count)
{
    *count = 0;

    if (!property_data || nitems < 2)
        return NULL;

    /* First pass: count icons */
    int icon_count = 0;
    unsigned long offset = 0;

    while (offset + 2 <= nitems) {
        unsigned long w = property_data[offset];
        unsigned long h = property_data[offset + 1];

        /* Sanity check dimensions */
        if (w == 0 || h == 0 || w > 1024 || h > 1024)
            break;

        unsigned long pixels = w * h;
        if (offset + 2 + pixels > nitems)
            break;

        icon_count++;
        offset += 2 + pixels;
    }

    if (icon_count == 0)
        return NULL;

    /* Allocate array */
    IconEntry *icons = malloc(sizeof(IconEntry) * icon_count);
    if (!icons)
        return NULL;

    /* Second pass: fill in entries */
    offset = 0;
    for (int i = 0; i < icon_count; i++) {
        icons[i].width = (int)property_data[offset];
        icons[i].height = (int)property_data[offset + 1];
        icons[i].data = &property_data[offset + 2];
        offset += 2 + (icons[i].width * icons[i].height);
    }

    *count = icon_count;
    return icons;
}

/*
 * Find the best icon size from available icons.
 * Prefers icons closest to desired_size, slightly larger is better than smaller.
 */
static IconEntry *icon_find_best(IconEntry *icons, int count, int desired_size)
{
    if (!icons || count == 0)
        return NULL;

    IconEntry *best = &icons[0];
    int best_diff = abs(best->width - desired_size);

    for (int i = 1; i < count; i++) {
        int diff = abs(icons[i].width - desired_size);

        /* Prefer this icon if it's closer to desired size */
        if (diff < best_diff) {
            best = &icons[i];
            best_diff = diff;
        }
        /* If same distance, prefer larger */
        else if (diff == best_diff && icons[i].width > best->width) {
            best = &icons[i];
        }
    }

    return best;
}

/*
 * Convert icon data to Cairo surface.
 * Handles ARGB to Cairo premultiplied alpha conversion.
 * Returns new Cairo surface (caller owns it).
 */
static cairo_surface_t *icon_to_cairo_surface(IconEntry *icon)
{
    if (!icon || icon->width <= 0 || icon->height <= 0)
        return NULL;

    int width = icon->width;
    int height = icon->height;

    /* Create Cairo image surface */
    cairo_surface_t *surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, width, height);

    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return NULL;
    }

    /* Get pointer to surface data */
    unsigned char *dest = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);

    /* Must call this before writing to surface data */
    cairo_surface_flush(surface);

    /*
     * Convert from _NET_WM_ICON format to Cairo ARGB32.
     *
     * _NET_WM_ICON stores each pixel as a 32-bit unsigned long in format:
     * bits 24-31: alpha, bits 16-23: red, bits 8-15: green, bits 0-7: blue
     *
     * Cairo ARGB32 expects premultiplied alpha in native byte order.
     * On little-endian (x86): bytes are B, G, R, A (low to high address)
     */
    for (int y = 0; y < height; y++) {
        uint32_t *row = (uint32_t *)(dest + y * stride);
        unsigned long *src_row = icon->data + y * width;

        for (int x = 0; x < width; x++) {
            unsigned long pixel = src_row[x];

            /* Extract ARGB components */
            unsigned int a = (pixel >> 24) & 0xFF;
            unsigned int r = (pixel >> 16) & 0xFF;
            unsigned int g = (pixel >> 8) & 0xFF;
            unsigned int b = pixel & 0xFF;

            /* Premultiply alpha */
            if (a < 255) {
                r = (r * a) / 255;
                g = (g * a) / 255;
                b = (b * a) / 255;
            }

            /* Pack into Cairo ARGB32 format (native byte order) */
            row[x] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }

    /* Notify Cairo that we've modified the surface data */
    cairo_surface_mark_dirty(surface);

    return surface;
}

/*
 * Scale a Cairo surface to the desired size.
 * Returns new scaled surface (caller owns it).
 */
static cairo_surface_t *icon_scale_surface(cairo_surface_t *source, int target_size)
{
    if (!source)
        return NULL;

    int src_width = cairo_image_surface_get_width(source);
    int src_height = cairo_image_surface_get_height(source);

    if (src_width == target_size && src_height == target_size)
        return cairo_surface_reference(source);

    /* Create new surface at target size */
    cairo_surface_t *scaled = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, target_size, target_size);

    if (cairo_surface_status(scaled) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(scaled);
        return NULL;
    }

    cairo_t *cr = cairo_create(scaled);

    /* Scale to fit, maintaining aspect ratio */
    double scale_x = (double)target_size / src_width;
    double scale_y = (double)target_size / src_height;
    double scale = (scale_x < scale_y) ? scale_x : scale_y;

    /* Center the icon */
    double offset_x = (target_size - src_width * scale) / 2.0;
    double offset_y = (target_size - src_height * scale) / 2.0;

    cairo_translate(cr, offset_x, offset_y);
    cairo_scale(cr, scale, scale);

    /* Use good quality filtering */
    cairo_set_source_surface(cr, source, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);

    cairo_paint(cr);
    cairo_destroy(cr);

    return scaled;
}

/*
 * Get window icon from _NET_WM_ICON property.
 * Returns Cairo surface at the requested size, or NULL if no icon.
 * Handles window destruction gracefully.
 */
cairo_surface_t *icon_get(AppContext *ctx, Window win, int size)
{
    if (!ctx || !ctx->display || win == None || size <= 0)
        return NULL;

    /* Read _NET_WM_ICON property with error handling */
    Atom actual_type;
    int actual_format;
    unsigned long nitems, bytes_after;
    unsigned char *data = NULL;

    /* Clear error state before X call */
    xerror_clear();

    int status = XGetWindowProperty(ctx->display, win,
                                    ctx->atoms->net_wm_icon,
                                    0, 0x7FFFFFFF, /* Read all data */
                                    False, XA_CARDINAL,
                                    &actual_type, &actual_format,
                                    &nitems, &bytes_after, &data);

    /* Sync and check for X errors */
    XSync(ctx->display, False);
    if (xerror_check()) {
        log_debug("X error getting icon for window 0x%lx", win);
        if (data)
            XFree(data);
        return NULL;
    }

    if (status != Success || actual_type == None || actual_format != 32 || !data) {
        if (data)
            XFree(data);
        return NULL;
    }

    /* Parse icon data to find all icons */
    int icon_count;
    IconEntry *icons = icon_parse_property((unsigned long *)data, nitems, &icon_count);

    if (!icons) {
        XFree(data);
        return NULL;
    }

    /* Find the best icon size */
    IconEntry *best = icon_find_best(icons, icon_count, size);

    cairo_surface_t *result = NULL;
    if (best) {
        /* Convert to Cairo surface */
        cairo_surface_t *raw = icon_to_cairo_surface(best);
        if (raw) {
            /* Scale to requested size if needed */
            result = icon_scale_surface(raw, size);
            cairo_surface_destroy(raw);
        }
    }

    free(icons);
    XFree(data);

    return result;
}

void icon_free(cairo_surface_t *surface)
{
    if (surface) {
        cairo_surface_destroy(surface);
    }
}
