/*
 * alttab - Alt+Tab window switcher
 * windows.c - Window enumeration and filtering
 */

#define _POSIX_C_SOURCE 200809L

#include "main.h"
#include "windows.h"
#include "layout.h"
#include <X11/Xatom.h>

/* Thumbnail capture parameters - must match overlay.c constants */
#define THUMB_ICON_SIZE        32
#define THUMB_ICON_PADDING     6
#define THUMB_TEXT_HEIGHT      24
#define THUMB_TEXT_PADDING     4
#define THUMB_TILE_PADDING     8    /* Internal padding for thumbnail area */

/* Additional window type atoms for filtering */
static Atom atom_wm_type_dock;
static Atom atom_wm_type_desktop;
static Atom atom_wm_type_toolbar;
static Atom atom_wm_type_menu;
static Atom atom_wm_type_splash;
static Atom atom_wm_type_utility;
static Atom atom_wm_type_dropdown_menu;
static Atom atom_wm_type_popup_menu;
static Atom atom_wm_type_tooltip;
static Atom atom_wm_type_notification;
static Atom atom_wm_type_combo;
static Atom atom_wm_type_dnd;
static bool atoms_initialized = false;

/*
 * Initialize window type atoms for filtering.
 * These are additional atoms beyond the basic ones in EwmhAtoms.
 */
static void windows_init_type_atoms(AppContext *ctx)
{
    if (atoms_initialized)
        return;

    atom_wm_type_dock = XInternAtom(ctx->display, "_NET_WM_WINDOW_TYPE_DOCK", False);
    atom_wm_type_desktop = XInternAtom(ctx->display, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    atom_wm_type_toolbar = XInternAtom(ctx->display, "_NET_WM_WINDOW_TYPE_TOOLBAR", False);
    atom_wm_type_menu = XInternAtom(ctx->display, "_NET_WM_WINDOW_TYPE_MENU", False);
    atom_wm_type_splash = XInternAtom(ctx->display, "_NET_WM_WINDOW_TYPE_SPLASH", False);
    atom_wm_type_utility = XInternAtom(ctx->display, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    atom_wm_type_dropdown_menu = XInternAtom(ctx->display, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", False);
    atom_wm_type_popup_menu = XInternAtom(ctx->display, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
    atom_wm_type_tooltip = XInternAtom(ctx->display, "_NET_WM_WINDOW_TYPE_TOOLTIP", False);
    atom_wm_type_notification = XInternAtom(ctx->display, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
    atom_wm_type_combo = XInternAtom(ctx->display, "_NET_WM_WINDOW_TYPE_COMBO", False);
    atom_wm_type_dnd = XInternAtom(ctx->display, "_NET_WM_WINDOW_TYPE_DND", False);

    atoms_initialized = true;
}

/*
 * Check if a window type should be filtered out.
 * Returns true if the window should be excluded from the list.
 */
static bool windows_should_filter_type(AppContext *ctx, Window win)
{
    Atom type = ewmh_get_window_type(ctx, win);

    /* Normal and dialog windows are acceptable */
    if (type == ctx->atoms->net_wm_window_type_normal ||
        type == ctx->atoms->net_wm_window_type_dialog) {
        return false;
    }

    /* Filter out dock, desktop, toolbar, menu, splash */
    if (type == atom_wm_type_dock ||
        type == atom_wm_type_desktop ||
        type == atom_wm_type_toolbar ||
        type == atom_wm_type_menu ||
        type == atom_wm_type_splash ||
        type == atom_wm_type_utility ||
        type == atom_wm_type_dropdown_menu ||
        type == atom_wm_type_popup_menu ||
        type == atom_wm_type_tooltip ||
        type == atom_wm_type_notification ||
        type == atom_wm_type_combo ||
        type == atom_wm_type_dnd) {
        return true;
    }

    /* Window without type property is treated as normal */
    return false;
}

/*
 * Check if window is on the current desktop or is sticky.
 * Returns true if the window should be visible in the switcher.
 */
static bool windows_is_on_desktop(AppContext *ctx, Window win, int current_desktop)
{
    int window_desktop = ewmh_get_window_desktop(ctx, win);

    /* Desktop = -1 from ewmh_get_window_desktop means sticky (0xFFFFFFFF) or no property */
    if (window_desktop == -1) {
        return true;
    }

    /* Check if window is on current desktop */
    return (window_desktop == current_desktop);
}

/*
 * Get window geometry.
 * Returns true on success, false if window was destroyed or error.
 */
static bool windows_get_geometry(AppContext *ctx, Window win, WindowInfo *info)
{
    Window root_return;
    int x, y;
    unsigned int width, height, border_width, depth;

    /* Set error handler to catch BadWindow errors */
    XSync(ctx->display, False);

    int status = XGetGeometry(ctx->display, win, &root_return,
                              &x, &y, &width, &height,
                              &border_width, &depth);

    if (!status) {
        return false;
    }

    info->x = x;
    info->y = y;
    info->width = (int)width;
    info->height = (int)height;

    return true;
}

/*
 * Free a single WindowInfo's allocated resources.
 */
static void window_info_free(WindowInfo *info)
{
    if (info->name) {
        free(info->name);
        info->name = NULL;
    }
    if (info->icon) {
        cairo_surface_destroy(info->icon);
        info->icon = NULL;
    }
    if (info->thumbnail) {
        cairo_surface_destroy(info->thumbnail);
        info->thumbnail = NULL;
    }
}

/*
 * Create and initialize a window list.
 */
WindowList *windows_list_create(void)
{
    WindowList *list = malloc(sizeof(WindowList));
    if (!list)
        return NULL;

    list->count = 0;
    list->capacity = 16;
    list->windows = malloc(sizeof(WindowInfo) * list->capacity);
    if (!list->windows) {
        free(list);
        return NULL;
    }

    return list;
}

/*
 * Free a window list and all its entries.
 */
void windows_list_free(WindowList *list)
{
    if (!list)
        return;

    for (int i = 0; i < list->count; i++) {
        window_info_free(&list->windows[i]);
    }

    free(list->windows);
    free(list);
}

/*
 * Add a window to the list, growing capacity if needed.
 * Returns true on success.
 */
static bool windows_list_add(WindowList *list, WindowInfo *info)
{
    if (list->count >= list->capacity) {
        int new_capacity = list->capacity * 2;
        WindowInfo *new_windows = realloc(list->windows,
                                          sizeof(WindowInfo) * new_capacity);
        if (!new_windows)
            return false;

        list->windows = new_windows;
        list->capacity = new_capacity;
    }

    list->windows[list->count] = *info;
    list->count++;
    return true;
}

/*
 * Calculate thumbnail dimensions for a given tile size.
 * Thumbnails are displayed in the upper area of the tile, leaving room for
 * the icon (bottom-left) and title (bottom center).
 */
static void calculate_thumbnail_size(int tile_width, int tile_height,
                                      int *thumb_width, int *thumb_height)
{
    /* Available height = tile height minus text area, icon area, and padding */
    int available_height = tile_height - THUMB_TEXT_HEIGHT - THUMB_TEXT_PADDING
                           - THUMB_TILE_PADDING * 2;

    /* Available width = tile width minus padding on both sides */
    int available_width = tile_width - THUMB_TILE_PADDING * 2;

    /* Ensure minimum dimensions */
    if (available_width < 40)
        available_width = 40;
    if (available_height < 30)
        available_height = 30;

    *thumb_width = available_width;
    *thumb_height = available_height;
}

/*
 * Get list of windows on the current desktop.
 * Filters out docks, panels, toolbars, and other non-switchable windows.
 * The overlay_window parameter allows filtering out our own overlay.
 * Returns a WindowList that must be freed with windows_list_free().
 */
WindowList *windows_get_list(AppContext *ctx, Window overlay_window)
{
    WindowList *list = windows_list_create();
    if (!list)
        return NULL;

    /* Initialize type atoms for filtering */
    windows_init_type_atoms(ctx);

    /* Get current desktop */
    int current_desktop = ewmh_get_current_desktop(ctx);

    /* Get client list from window manager */
    int client_count;
    Window *clients = ewmh_get_client_list(ctx, &client_count);
    if (!clients || client_count == 0) {
        if (clients)
            XFree(clients);
        return list;  /* Return empty list */
    }

    /* Calculate layout to determine thumbnail size */
    int screen_width = DisplayWidth(ctx->display, ctx->screen);
    int screen_height = DisplayHeight(ctx->display, ctx->screen);
    LayoutParams layout;
    layout_calculate(&layout, client_count, screen_width, screen_height);

    int thumb_width, thumb_height;
    calculate_thumbnail_size(layout.tile_width, layout.tile_height,
                             &thumb_width, &thumb_height);

    /* Initialize thumbnail subsystem */
    bool thumbnails_available = thumbnail_init(ctx) && thumbnail_available(ctx);

    /* Iterate through clients in reverse (stacking order, top first) */
    for (int i = client_count - 1; i >= 0; i--) {
        Window win = clients[i];

        /* Skip our own overlay window */
        if (win == overlay_window)
            continue;

        /* Skip windows not on current desktop */
        if (!windows_is_on_desktop(ctx, win, current_desktop))
            continue;

        /* Skip dock, panel, toolbar, etc. window types */
        if (windows_should_filter_type(ctx, win))
            continue;

        /* Build WindowInfo */
        WindowInfo info = {0};
        info.window = win;

        /* Get window geometry - skip if window was destroyed */
        if (!windows_get_geometry(ctx, win, &info))
            continue;

        /* Get window name */
        info.name = ewmh_get_window_name(ctx, win);

        /* Get desktop number */
        info.desktop = ewmh_get_window_desktop(ctx, win);

        /* Check if window is minimized/hidden */
        info.hidden = ewmh_is_window_hidden(ctx, win);

        /* Fetch icon for this window (32px is our target size) */
        info.icon = icon_get(ctx, win, 32);

        /* Capture thumbnail if available and window is not minimized */
        info.thumbnail = NULL;
        if (thumbnails_available && !info.hidden) {
            info.thumbnail = thumbnail_capture(ctx, win, thumb_width, thumb_height);
            if (info.thumbnail) {
                log_debug("Captured thumbnail for window 0x%lx (%dx%d)",
                          win, thumb_width, thumb_height);
            }
        }

        /* Add to list */
        if (!windows_list_add(list, &info)) {
            /* Failed to add - free resources and continue */
            if (info.name)
                free(info.name);
            if (info.icon)
                cairo_surface_destroy(info.icon);
            if (info.thumbnail)
                cairo_surface_destroy(info.thumbnail);
            continue;
        }
    }

    XFree(clients);
    return list;
}

/* ============================================================
 * MRU (Most Recently Used) List Implementation
 * ============================================================ */

/*
 * Hash function for window IDs.
 * Simple modulo hashing - window IDs are typically sequential.
 */
static unsigned int mru_hash(Window win)
{
    return (unsigned int)(win % MRU_HASH_SIZE);
}

/*
 * Find a node in the MRU list by window ID.
 * Returns NULL if not found.
 */
static MRUNode *mru_find(MRUList *list, Window win)
{
    unsigned int hash = mru_hash(win);
    MRUNode *node = list->hash_table[hash];

    while (node) {
        if (node->window == win)
            return node;
        node = node->hash_next;
    }

    return NULL;
}

/*
 * Add a node to the hash table.
 */
static void mru_hash_add(MRUList *list, MRUNode *node)
{
    unsigned int hash = mru_hash(node->window);
    node->hash_next = list->hash_table[hash];
    list->hash_table[hash] = node;
}

/*
 * Remove a node from the hash table.
 */
static void mru_hash_remove(MRUList *list, MRUNode *node)
{
    unsigned int hash = mru_hash(node->window);
    MRUNode **pp = &list->hash_table[hash];

    while (*pp) {
        if (*pp == node) {
            *pp = node->hash_next;
            node->hash_next = NULL;
            return;
        }
        pp = &(*pp)->hash_next;
    }
}

/*
 * Remove a node from the doubly-linked list (but not the hash table).
 */
static void mru_unlink(MRUList *list, MRUNode *node)
{
    if (node->prev)
        node->prev->next = node->next;
    else
        list->head = node->next;

    if (node->next)
        node->next->prev = node->prev;
    else
        list->tail = node->prev;

    node->prev = NULL;
    node->next = NULL;
}

/*
 * Insert a node at the front of the doubly-linked list.
 */
static void mru_insert_front(MRUList *list, MRUNode *node)
{
    node->prev = NULL;
    node->next = list->head;

    if (list->head)
        list->head->prev = node;
    else
        list->tail = node;

    list->head = node;
}

/*
 * Initialize an empty MRU list.
 */
MRUList *mru_init(void)
{
    MRUList *list = calloc(1, sizeof(MRUList));
    if (!list)
        return NULL;

    /* calloc zeros everything, so hash_table, head, tail, count are all 0/NULL */
    return list;
}

/*
 * Free all resources in the MRU list.
 */
void mru_free(MRUList *list)
{
    if (!list)
        return;

    MRUNode *node = list->head;
    while (node) {
        MRUNode *next = node->next;
        free(node);
        node = next;
    }

    free(list);
}

/*
 * Touch a window - move it to the front of the MRU list (most recent).
 * If the window is not in the list, add it.
 */
void mru_touch(MRUList *list, Window win)
{
    if (!list || win == None)
        return;

    MRUNode *node = mru_find(list, win);

    if (node) {
        /* Window exists - move to front if not already there */
        if (node != list->head) {
            mru_unlink(list, node);
            mru_insert_front(list, node);
        }
    } else {
        /* New window - create node and add to front */
        node = calloc(1, sizeof(MRUNode));
        if (!node)
            return;

        node->window = win;
        mru_insert_front(list, node);
        mru_hash_add(list, node);
        list->count++;
    }
}

/*
 * Remove a window from the MRU list (e.g., when window is destroyed).
 */
void mru_remove(MRUList *list, Window win)
{
    if (!list || win == None)
        return;

    MRUNode *node = mru_find(list, win);
    if (!node)
        return;

    mru_unlink(list, node);
    mru_hash_remove(list, node);
    list->count--;
    free(node);
}

/*
 * Get windows in MRU order (most recent first).
 * Returns a newly allocated array of Window IDs.
 * Caller must free the returned array.
 * Sets *count to the number of windows.
 */
Window *mru_get_order(MRUList *list, int *count)
{
    *count = 0;

    if (!list || list->count == 0)
        return NULL;

    Window *windows = malloc(sizeof(Window) * list->count);
    if (!windows)
        return NULL;

    int i = 0;
    MRUNode *node = list->head;
    while (node && i < list->count) {
        windows[i++] = node->window;
        node = node->next;
    }

    *count = i;
    return windows;
}

/*
 * Initialize MRU list from the current stacking order.
 * Uses _NET_CLIENT_LIST_STACKING which lists windows from bottom to top,
 * so we iterate in reverse to get the topmost (most recently used) first.
 */
void mru_init_from_stacking(MRUList *list, AppContext *ctx)
{
    if (!list || !ctx)
        return;

    int client_count;
    Window *clients = ewmh_get_client_list(ctx, &client_count);
    if (!clients || client_count == 0) {
        if (clients)
            XFree(clients);
        return;
    }

    /* Add windows in reverse stacking order (top to bottom = MRU to LRU) */
    for (int i = client_count - 1; i >= 0; i--) {
        mru_touch(list, clients[i]);
    }

    XFree(clients);
}

/* ============================================================
 * Window Stacking Order Save/Restore
 * ============================================================ */

/*
 * Save the current window stacking order.
 * Uses _NET_CLIENT_LIST_STACKING which gives windows from bottom to top.
 * Returns a StackingOrder struct that must be freed with stacking_free().
 */
StackingOrder *stacking_save(AppContext *ctx)
{
    if (!ctx)
        return NULL;

    StackingOrder *order = malloc(sizeof(StackingOrder));
    if (!order)
        return NULL;

    order->windows = NULL;
    order->count = 0;

    int client_count;
    Window *clients = ewmh_get_client_list(ctx, &client_count);
    if (!clients || client_count == 0) {
        if (clients)
            XFree(clients);
        return order;
    }

    /* Allocate and copy the window list */
    order->windows = malloc(sizeof(Window) * client_count);
    if (!order->windows) {
        XFree(clients);
        free(order);
        return NULL;
    }

    memcpy(order->windows, clients, sizeof(Window) * client_count);
    order->count = client_count;

    XFree(clients);
    log_debug("Saved stacking order: %d windows", order->count);

    return order;
}

/*
 * Restore a previously saved stacking order.
 * Re-stacks windows from bottom to top using XRestackWindows.
 */
void stacking_restore(AppContext *ctx, StackingOrder *order)
{
    if (!ctx || !order || !order->windows || order->count == 0)
        return;

    /*
     * XRestackWindows expects windows in top-to-bottom order,
     * but our saved order is bottom-to-top. Reverse it.
     */
    Window *reversed = malloc(sizeof(Window) * order->count);
    if (!reversed)
        return;

    for (int i = 0; i < order->count; i++) {
        reversed[i] = order->windows[order->count - 1 - i];
    }

    /* Restack all windows */
    XRestackWindows(ctx->display, reversed, order->count);
    XFlush(ctx->display);

    free(reversed);
    log_debug("Restored stacking order: %d windows", order->count);
}

/*
 * Free a StackingOrder struct.
 */
void stacking_free(StackingOrder *order)
{
    if (!order)
        return;

    if (order->windows)
        free(order->windows);

    free(order);
}

/*
 * Raise a window for preview, keeping the overlay above it.
 * This temporarily brings the selected window to the top of the stack,
 * then re-raises the overlay to stay above it.
 */
void raise_preview_window(AppContext *ctx, Window win, Window overlay_window)
{
    if (!ctx || win == None)
        return;

    /* Raise the selected window */
    XRaiseWindow(ctx->display, win);

    /* Re-raise the overlay window to keep it on top */
    if (overlay_window != None) {
        XRaiseWindow(ctx->display, overlay_window);
    }

    XFlush(ctx->display);
    log_debug("Raised preview window 0x%lx", win);
}
