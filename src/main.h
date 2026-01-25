/*
 * alttab - Alt+Tab window switcher
 * main.h - Common includes and forward declarations
 */

#ifndef ALTTAB_MAIN_H
#define ALTTAB_MAIN_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>

/* Forward declarations */
typedef struct AppContext AppContext;
typedef struct EwmhAtoms EwmhAtoms;
typedef struct WindowInfo WindowInfo;
typedef struct WindowList WindowList;
typedef struct MRUNode MRUNode;
typedef struct MRUList MRUList;

/* Application context - global state */
struct AppContext {
    Display *display;
    Window root;
    int screen;
    EwmhAtoms *atoms;
    WindowList *windows;
    MRUList *mru;           /* MRU tracking for window order */
    int selected_index;
    bool switcher_active;
};

/* EWMH atoms */
struct EwmhAtoms {
    Atom net_client_list;
    Atom net_client_list_stacking;
    Atom net_current_desktop;
    Atom net_wm_desktop;
    Atom net_wm_name;
    Atom net_wm_icon;
    Atom net_wm_state;
    Atom net_wm_state_hidden;
    Atom net_wm_state_above;
    Atom net_wm_state_skip_taskbar;
    Atom net_wm_state_skip_pager;
    Atom net_wm_window_type;
    Atom net_wm_window_type_normal;
    Atom net_wm_window_type_dialog;
    Atom net_active_window;
    Atom utf8_string;
    Atom wm_name;
};

/* Window information */
struct WindowInfo {
    Window window;
    char *name;
    int desktop;
    int x, y;
    int width, height;
    bool hidden;
    cairo_surface_t *icon;
    cairo_surface_t *thumbnail;
};

/* Window list */
struct WindowList {
    WindowInfo *windows;
    int count;
    int capacity;
};

/* Module function prototypes */

/* ewmh.c */
void ewmh_init_atoms(AppContext *ctx);
Window *ewmh_get_client_list(AppContext *ctx, int *count);
int ewmh_get_current_desktop(AppContext *ctx);
int ewmh_get_window_desktop(AppContext *ctx, Window win);
char *ewmh_get_window_name(AppContext *ctx, Window win);
Atom ewmh_get_window_type(AppContext *ctx, Window win);
bool ewmh_is_window_hidden(AppContext *ctx, Window win);
void ewmh_activate_window(AppContext *ctx, Window win);
Window ewmh_get_active_window(AppContext *ctx);

/* hotkey.c */
bool hotkey_grab(AppContext *ctx);
void hotkey_ungrab(AppContext *ctx);
bool hotkey_alt_released(AppContext *ctx);

/* overlay.c */
bool overlay_create(AppContext *ctx);
void overlay_destroy(AppContext *ctx);
void overlay_show(AppContext *ctx);
void overlay_hide(AppContext *ctx);
void overlay_draw(AppContext *ctx);
void overlay_clear(AppContext *ctx);
void overlay_draw_tile(AppContext *ctx, int x, int y, int width, int height,
                       const char *title, bool selected, cairo_surface_t *icon);
void overlay_flush(AppContext *ctx);
void redraw_overlay(AppContext *ctx);

/* thumbnail.c */
bool thumbnail_init(AppContext *ctx);
void thumbnail_cleanup(AppContext *ctx);
bool thumbnail_available(AppContext *ctx);
cairo_surface_t *thumbnail_capture(AppContext *ctx, Window win, int width, int height);
void thumbnail_free(cairo_surface_t *surface);

/* icon.c */
cairo_surface_t *icon_get(AppContext *ctx, Window win, int size);
void icon_free(cairo_surface_t *surface);

/* windows.c */
WindowList *windows_list_create(void);
void windows_list_free(WindowList *list);
WindowList *windows_get_list(AppContext *ctx, Window overlay_window);

/* MRU list functions (defined in windows.h, but declared here for convenience) */
MRUList *mru_init(void);
void mru_free(MRUList *list);
void mru_touch(MRUList *list, Window win);
void mru_remove(MRUList *list, Window win);
Window *mru_get_order(MRUList *list, int *count);
void mru_init_from_stacking(MRUList *list, AppContext *ctx);

/* util.c */
void log_error(const char *fmt, ...);
void log_warning(const char *fmt, ...);
void log_debug(const char *fmt, ...);

/* layout.c - forward declaration of LayoutParams in layout.h */

#endif /* ALTTAB_MAIN_H */
