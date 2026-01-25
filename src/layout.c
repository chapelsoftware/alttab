/*
 * alttab - Alt+Tab window switcher
 * layout.c - Grid layout calculation for window tiles
 */

#include "layout.h"

/*
 * Calculate layout parameters for given window count and screen dimensions.
 *
 * Algorithm:
 * 1. Calculate max overlay dimensions (80% of screen)
 * 2. Determine tiles per row that fit within max width
 * 3. Calculate rows needed
 * 4. Scale tiles if they don't fit within max height
 * 5. Compute final overlay dimensions and center position
 */
void layout_calculate(LayoutParams *params, int window_count,
                      int screen_width, int screen_height)
{
    /* Handle edge case: no windows */
    if (window_count <= 0) {
        memset(params, 0, sizeof(*params));
        params->overlay_x = screen_width / 2;
        params->overlay_y = screen_height / 2;
        params->scale = 1.0;
        return;
    }

    /* Initialize with base values */
    params->tile_width = LAYOUT_BASE_TILE_WIDTH;
    params->tile_height = LAYOUT_BASE_TILE_HEIGHT;
    params->margin = LAYOUT_TILE_MARGIN;
    params->padding = LAYOUT_PADDING;
    params->scale = 1.0;

    /* Calculate maximum available space (80% of screen) */
    int max_width = (int)(screen_width * LAYOUT_MAX_WIDTH_RATIO);
    int max_height = (int)(screen_height * LAYOUT_MAX_HEIGHT_RATIO);

    /* Calculate content width available for tiles */
    int content_max_width = max_width - 2 * params->padding;
    int content_max_height = max_height - 2 * params->padding;

    /* Single tile width including margin on one side (for calculation) */
    int tile_with_margin = params->tile_width + params->margin;

    /* Calculate how many tiles fit per row */
    /* Formula: tiles_per_row * tile_width + (tiles_per_row - 1) * margin <= content_max_width */
    /* tiles_per_row * (tile_width + margin) - margin <= content_max_width */
    /* tiles_per_row <= (content_max_width + margin) / (tile_width + margin) */
    params->tiles_per_row = (content_max_width + params->margin) / tile_with_margin;

    /* Ensure at least 1 tile per row */
    if (params->tiles_per_row < 1)
        params->tiles_per_row = 1;

    /* Don't have more tiles per row than windows */
    if (params->tiles_per_row > window_count)
        params->tiles_per_row = window_count;

    /* Calculate number of rows needed */
    params->num_rows = (window_count + params->tiles_per_row - 1) / params->tiles_per_row;

    /* Calculate grid height with current tile size */
    int grid_height = params->num_rows * params->tile_height +
                      (params->num_rows - 1) * params->margin;

    /* Check if we need to scale down tiles to fit height */
    if (grid_height > content_max_height && params->num_rows > 0) {
        /* Calculate scale factor needed to fit */
        double scale = (double)content_max_height / grid_height;

        /* Apply scale to tile dimensions */
        params->tile_width = (int)(LAYOUT_BASE_TILE_WIDTH * scale);
        params->tile_height = (int)(LAYOUT_BASE_TILE_HEIGHT * scale);
        params->margin = (int)(LAYOUT_TILE_MARGIN * scale);
        params->scale = scale;

        /* Minimum tile size to ensure usability */
        if (params->tile_width < 80) {
            params->tile_width = 80;
            params->tile_height = 60;
            params->margin = 5;
            params->scale = 80.0 / LAYOUT_BASE_TILE_WIDTH;
        }

        /* Recalculate tiles per row with scaled dimensions */
        tile_with_margin = params->tile_width + params->margin;
        params->tiles_per_row = (content_max_width + params->margin) / tile_with_margin;
        if (params->tiles_per_row < 1)
            params->tiles_per_row = 1;
        if (params->tiles_per_row > window_count)
            params->tiles_per_row = window_count;

        /* Recalculate rows */
        params->num_rows = (window_count + params->tiles_per_row - 1) / params->tiles_per_row;
    }

    /* Calculate actual grid dimensions */
    int grid_width = params->tiles_per_row * params->tile_width +
                     (params->tiles_per_row - 1) * params->margin;
    grid_height = params->num_rows * params->tile_height +
                  (params->num_rows - 1) * params->margin;

    /* Calculate overlay dimensions (grid + padding) */
    params->overlay_width = grid_width + 2 * params->padding;
    params->overlay_height = grid_height + 2 * params->padding;

    /* Center overlay on screen */
    params->overlay_x = (screen_width - params->overlay_width) / 2;
    params->overlay_y = (screen_height - params->overlay_height) / 2;
}

/*
 * Get the x,y position for a tile at the given index.
 * Position is relative to the overlay window origin.
 */
void layout_get_tile_position(const LayoutParams *params, int index,
                              int *x, int *y)
{
    if (!params || params->tiles_per_row <= 0) {
        *x = 0;
        *y = 0;
        return;
    }

    /* Calculate row and column */
    int col = index % params->tiles_per_row;
    int row = index / params->tiles_per_row;

    /* Calculate position with padding and margins */
    *x = params->padding + col * (params->tile_width + params->margin);
    *y = params->padding + row * (params->tile_height + params->margin);
}

/*
 * Get the total overlay dimensions.
 */
void layout_get_overlay_size(const LayoutParams *params,
                             int *width, int *height)
{
    if (params) {
        *width = params->overlay_width;
        *height = params->overlay_height;
    } else {
        *width = 0;
        *height = 0;
    }
}
