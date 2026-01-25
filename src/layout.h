/*
 * alttab - Alt+Tab window switcher
 * layout.h - Grid layout calculation for window tiles
 */

#ifndef ALTTAB_LAYOUT_H
#define ALTTAB_LAYOUT_H

#include "main.h"

/* Default layout constants */
#define LAYOUT_BASE_TILE_WIDTH   200
#define LAYOUT_BASE_TILE_HEIGHT  150
#define LAYOUT_TILE_MARGIN       10    /* Space between tiles */
#define LAYOUT_PADDING           20    /* Padding around grid */
#define LAYOUT_MAX_WIDTH_RATIO   0.80  /* Max 80% of screen width */
#define LAYOUT_MAX_HEIGHT_RATIO  0.80  /* Max 80% of screen height */

/* Layout parameters computed for a given window count and screen size */
typedef struct LayoutParams {
    int tile_width;       /* Actual tile width (may be scaled) */
    int tile_height;      /* Actual tile height (may be scaled) */
    int tiles_per_row;    /* Number of tiles in each row */
    int num_rows;         /* Number of rows */
    int margin;           /* Space between tiles */
    int padding;          /* Padding around the grid */
    int overlay_width;    /* Total overlay width */
    int overlay_height;   /* Total overlay height */
    int overlay_x;        /* Overlay X position (centered) */
    int overlay_y;        /* Overlay Y position (centered) */
    double scale;         /* Scale factor applied (1.0 = no scaling) */
} LayoutParams;

/*
 * Calculate layout parameters for given window count and screen dimensions.
 * Computes optimal tile arrangement that fits within 80% of screen.
 */
void layout_calculate(LayoutParams *params, int window_count,
                      int screen_width, int screen_height);

/*
 * Get the x,y position for a tile at the given index.
 * Index is 0-based, counted left-to-right, top-to-bottom.
 */
void layout_get_tile_position(const LayoutParams *params, int index,
                              int *x, int *y);

/*
 * Get the total overlay dimensions.
 * Convenience function that returns overlay_width and overlay_height.
 */
void layout_get_overlay_size(const LayoutParams *params,
                             int *width, int *height);

#endif /* ALTTAB_LAYOUT_H */
