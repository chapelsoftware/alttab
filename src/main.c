/*
 * alttab - Alt+Tab window switcher
 * main.c - Entry point and main event loop
 */

#include "main.h"
#include "overlay.h"
#include "util.h"
#include <X11/keysym.h>
#include <poll.h>
#include <unistd.h>
#include <signal.h>

/* Application state machine */
typedef enum {
    STATE_IDLE,
    STATE_SWITCHING
} AppState;

/* Polling interval for Alt key release (milliseconds) */
#define ALT_POLL_INTERVAL_MS 10

/* Global flag for clean shutdown on signals or display disconnect */
static volatile sig_atomic_t g_running = 1;

/* Forward declarations */
static void start_switching(AppContext *ctx, Window *original_window,
                            StackingOrder **saved_stacking);
static void stop_switching(AppContext *ctx, bool confirm, Window original_window,
                           StackingOrder **saved_stacking);
static void select_next(AppContext *ctx);
static void select_previous(AppContext *ctx);
static void raise_current_selection(AppContext *ctx);
static void handle_keypress(AppContext *ctx, XKeyEvent *ev, AppState *state,
                            Window *original_window, StackingOrder **saved_stacking);
static bool validate_window_exists(AppContext *ctx, Window win);

/*
 * Signal handler for clean shutdown.
 */
static void signal_handler(int sig)
{
    (void)sig;
    g_running = 0;
}

/*
 * Display disconnect callback - called when X connection is lost.
 */
static void on_display_disconnect(void *user_data)
{
    (void)user_data;
    log_error("Display connection lost - shutting down");
    g_running = 0;
}

/*
 * Check if a window still exists (hasn't been destroyed).
 * Returns true if window is valid.
 */
static bool validate_window_exists(AppContext *ctx, Window win)
{
    if (win == None)
        return false;

    XWindowAttributes attrs;
    xerror_clear();
    Status status = XGetWindowAttributes(ctx->display, win, &attrs);
    XSync(ctx->display, False);

    return (status != 0 && xerror_check() == 0);
}

/*
 * Remove a window from the current window list by its Window ID.
 * Adjusts selected_index if needed.
 */
static void remove_window_from_list(AppContext *ctx, Window win)
{
    if (!ctx->windows || ctx->windows->count == 0)
        return;

    /* Find the window in the list */
    int found_index = -1;
    for (int i = 0; i < ctx->windows->count; i++) {
        if (ctx->windows->windows[i].window == win) {
            found_index = i;
            break;
        }
    }

    if (found_index < 0)
        return;

    log_debug("Removing destroyed window 0x%lx from list (index %d)", win, found_index);

    /* Free resources for this window */
    WindowInfo *info = &ctx->windows->windows[found_index];
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

    /* Shift remaining windows down */
    for (int i = found_index; i < ctx->windows->count - 1; i++) {
        ctx->windows->windows[i] = ctx->windows->windows[i + 1];
    }
    ctx->windows->count--;

    /* Adjust selected index */
    if (ctx->selected_index >= ctx->windows->count) {
        ctx->selected_index = ctx->windows->count - 1;
        if (ctx->selected_index < 0)
            ctx->selected_index = 0;
    } else if (ctx->selected_index > found_index) {
        ctx->selected_index--;
    }
}


/*
 * Start the window switching mode.
 * Captures the currently active window, builds the window list, and shows overlay.
 * Saves the original stacking order for potential cancel/restore.
 */
static void start_switching(AppContext *ctx, Window *original_window,
                            StackingOrder **saved_stacking)
{
    OverlayState *overlay = overlay_get_state();

    /* Remember the currently active window for potential cancel */
    *original_window = ewmh_get_active_window(ctx);

    /* Save the current stacking order before we start modifying it */
    if (*saved_stacking) {
        stacking_free(*saved_stacking);
    }
    *saved_stacking = stacking_save(ctx);

    /* Get the list of windows, excluding our overlay */
    if (ctx->windows) {
        windows_list_free(ctx->windows);
        ctx->windows = NULL;
    }
    ctx->windows = windows_get_list(ctx, overlay->window);

    if (!ctx->windows || ctx->windows->count == 0) {
        log_debug("No windows to switch to");
        return;
    }

    /* Start with second window selected (first is current) */
    ctx->selected_index = (ctx->windows->count > 1) ? 1 : 0;
    ctx->switcher_active = true;

    /* Show and draw the overlay */
    overlay_show(ctx);
    redraw_overlay(ctx);

    /* Raise the initially selected window for preview */
    raise_current_selection(ctx);
}

/*
 * Stop the window switching mode.
 * If confirm is true, activate the selected window.
 * If confirm is false, restore the original window and stacking order.
 */
static void stop_switching(AppContext *ctx, bool confirm, Window original_window,
                           StackingOrder **saved_stacking)
{
    ctx->switcher_active = false;

    /* Hide overlay first */
    overlay_hide(ctx);

    if (confirm && ctx->windows && ctx->windows->count > 0 &&
        ctx->selected_index >= 0 && ctx->selected_index < ctx->windows->count) {
        /* Activate the selected window */
        Window selected = ctx->windows->windows[ctx->selected_index].window;
        ewmh_activate_window(ctx, selected);
        log_debug("Activated window 0x%lx", selected);
    } else if (!confirm) {
        /* Cancel - restore original stacking order first */
        if (*saved_stacking) {
            stacking_restore(ctx, *saved_stacking);
        }
        /* Then restore original window focus */
        if (original_window != None) {
            ewmh_activate_window(ctx, original_window);
            log_debug("Restored original window 0x%lx", original_window);
        }
    }

    /* Free saved stacking order */
    if (*saved_stacking) {
        stacking_free(*saved_stacking);
        *saved_stacking = NULL;
    }

    /* Free window list */
    if (ctx->windows) {
        windows_list_free(ctx->windows);
        ctx->windows = NULL;
    }
}

/*
 * Raise the currently selected window for preview.
 * This makes the selected window visible behind the overlay.
 */
static void raise_current_selection(AppContext *ctx)
{
    if (!ctx->windows || ctx->windows->count == 0)
        return;

    if (ctx->selected_index < 0 || ctx->selected_index >= ctx->windows->count)
        return;

    OverlayState *overlay = overlay_get_state();
    Window selected_win = ctx->windows->windows[ctx->selected_index].window;
    raise_preview_window(ctx, selected_win, overlay->window);
}

/*
 * Select the next window in the list (wrap around).
 */
static void select_next(AppContext *ctx)
{
    if (!ctx->windows || ctx->windows->count == 0)
        return;

    ctx->selected_index = (ctx->selected_index + 1) % ctx->windows->count;
    redraw_overlay(ctx);
    raise_current_selection(ctx);
    log_debug("Selected next: index %d", ctx->selected_index);
}

/*
 * Select the previous window in the list (wrap around).
 */
static void select_previous(AppContext *ctx)
{
    if (!ctx->windows || ctx->windows->count == 0)
        return;

    ctx->selected_index--;
    if (ctx->selected_index < 0)
        ctx->selected_index = ctx->windows->count - 1;
    redraw_overlay(ctx);
    raise_current_selection(ctx);
    log_debug("Selected previous: index %d", ctx->selected_index);
}

/*
 * Handle a KeyPress event.
 */
static void handle_keypress(AppContext *ctx, XKeyEvent *ev, AppState *state,
                            Window *original_window, StackingOrder **saved_stacking)
{
    KeySym keysym = XLookupKeysym(ev, 0);
    bool shift_pressed = (ev->state & ShiftMask) != 0;

    if (*state == STATE_IDLE) {
        /* In idle state, Alt+Tab starts switching */
        if (keysym == XK_Tab || keysym == XK_ISO_Left_Tab) {
            *state = STATE_SWITCHING;
            start_switching(ctx, original_window, saved_stacking);

            /* If Shift is held, go to previous (which wraps to last) */
            if (shift_pressed && ctx->windows && ctx->windows->count > 1) {
                /* Start at index 1, go back to last */
                ctx->selected_index = ctx->windows->count - 1;
                redraw_overlay(ctx);
                raise_current_selection(ctx);
            }
        }
    } else {
        /* In switching state, handle navigation keys */
        switch (keysym) {
        case XK_Tab:
            if (shift_pressed) {
                select_previous(ctx);
            } else {
                select_next(ctx);
            }
            break;

        case XK_ISO_Left_Tab:
            /* ISO_Left_Tab is generated by Shift+Tab on some keyboards */
            select_previous(ctx);
            break;

        case XK_Escape:
            /* Cancel and restore original window and stacking order */
            stop_switching(ctx, false, *original_window, saved_stacking);
            *state = STATE_IDLE;
            break;

        case XK_Return:
        case XK_KP_Enter:
            /* Confirm selection */
            stop_switching(ctx, true, *original_window, saved_stacking);
            *state = STATE_IDLE;
            break;

        default:
            break;
        }
    }
}

int main(int argc, char *argv[])
{
    AppContext ctx = {0};
    AppState state = STATE_IDLE;
    Window original_window = None;
    StackingOrder *saved_stacking = NULL;

    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            log_set_level(LOG_LEVEL_INFO);
        } else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            log_set_level(LOG_LEVEL_DEBUG);
        } else if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
            log_set_level(LOG_LEVEL_ERROR);
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  -v, --verbose  Show info messages\n");
            printf("  -d, --debug    Show debug messages\n");
            printf("  -q, --quiet    Show only errors\n");
            printf("  -h, --help     Show this help\n");
            return 0;
        }
    }

    /* Install signal handlers for clean shutdown */
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    /* Open X display */
    ctx.display = XOpenDisplay(NULL);
    if (!ctx.display) {
        log_error("Cannot open X display");
        return 1;
    }

    /* Install error handlers before any other X operations */
    xerror_install_handler(ctx.display);
    xioerror_install_handler(ctx.display);
    set_display_disconnect_callback(on_display_disconnect, &ctx);

    ctx.screen = DefaultScreen(ctx.display);
    ctx.root = RootWindow(ctx.display, ctx.screen);

    log_info("Connected to X display, screen %d", ctx.screen);

    /* Initialize EWMH atoms */
    ewmh_init_atoms(&ctx);

    /* Grab hotkey */
    if (!hotkey_grab(&ctx)) {
        log_error("Failed to grab hotkey");
        XCloseDisplay(ctx.display);
        return 1;
    }

    /* Create overlay window */
    if (!overlay_create(&ctx)) {
        log_error("Failed to create overlay");
        hotkey_ungrab(&ctx);
        XCloseDisplay(ctx.display);
        return 1;
    }

    /* Select PropertyNotify on root for _NET_ACTIVE_WINDOW changes,
     * and SubstructureNotify for window create/destroy events */
    XSelectInput(ctx.display, ctx.root,
                 PropertyChangeMask | SubstructureNotifyMask);

    /* Get X connection file descriptor for poll() */
    int x_fd = ConnectionNumber(ctx.display);

    log_info("Entering main event loop");

    /* Main event loop */
    while (g_running) {
        if (state == STATE_SWITCHING) {
            /* When switching, poll for Alt release and X events */
            struct pollfd pfd = {
                .fd = x_fd,
                .events = POLLIN,
                .revents = 0
            };

            /* Process any pending X events first */
            while (XPending(ctx.display)) {
                XEvent ev;
                XNextEvent(ctx.display, &ev);

                switch (ev.type) {
                case KeyPress:
                    handle_keypress(&ctx, &ev.xkey, &state, &original_window,
                                    &saved_stacking);
                    break;

                case Expose:
                    /* Redraw on expose */
                    if (ctx.switcher_active) {
                        redraw_overlay(&ctx);
                    }
                    break;

                case DestroyNotify:
                    /* A window was destroyed - remove from list if present */
                    if (ctx.switcher_active && ctx.windows) {
                        Window destroyed = ev.xdestroywindow.window;
                        log_debug("Window 0x%lx destroyed during switching", destroyed);
                        remove_window_from_list(&ctx, destroyed);

                        if (ctx.windows->count == 0) {
                            /* No windows left */
                            log_debug("No windows left after destruction");
                            stop_switching(&ctx, false, original_window, &saved_stacking);
                            state = STATE_IDLE;
                        } else {
                            redraw_overlay(&ctx);
                            raise_current_selection(&ctx);
                        }
                    }
                    break;

                case UnmapNotify:
                    /* Window was unmapped (closed/minimized) - check if it's in our list */
                    if (ctx.switcher_active && ctx.windows) {
                        /* Some WMs send UnmapNotify before DestroyNotify */
                        Window unmapped = ev.xunmap.window;
                        if (!validate_window_exists(&ctx, unmapped)) {
                            log_debug("Window 0x%lx unmapped and invalid", unmapped);
                            remove_window_from_list(&ctx, unmapped);
                            if (ctx.windows->count == 0) {
                                stop_switching(&ctx, false, original_window, &saved_stacking);
                                state = STATE_IDLE;
                            } else {
                                redraw_overlay(&ctx);
                                raise_current_selection(&ctx);
                            }
                        }
                    }
                    break;

                default:
                    break;
                }
            }

            /* Check if still in switching state after processing events */
            if (state != STATE_SWITCHING)
                continue;

            /* Poll with timeout to check for Alt release */
            int ret = poll(&pfd, 1, ALT_POLL_INTERVAL_MS);
            if (ret < 0) {
                if (g_running) {
                    log_error("poll() failed");
                }
                break;
            }

            /* Check if Alt was released */
            if (hotkey_alt_released(&ctx)) {
                log_debug("Alt released - confirming selection");
                stop_switching(&ctx, true, original_window, &saved_stacking);
                state = STATE_IDLE;
            }
        } else {
            /* In idle state, block on X events with timeout to check for signals */
            struct pollfd pfd = {
                .fd = x_fd,
                .events = POLLIN,
                .revents = 0
            };

            int ret = poll(&pfd, 1, 100); /* 100ms timeout to check signals */
            if (ret < 0) {
                if (g_running) {
                    log_error("poll() failed");
                }
                break;
            }

            if (ret == 0 || !(pfd.revents & POLLIN))
                continue;

            XEvent ev;
            XNextEvent(ctx.display, &ev);

            switch (ev.type) {
            case KeyPress:
                handle_keypress(&ctx, &ev.xkey, &state, &original_window,
                                &saved_stacking);
                break;

            case PropertyNotify:
                /* Could handle _NET_ACTIVE_WINDOW changes here for MRU tracking */
                break;

            default:
                break;
            }
        }
    }

    log_info("Shutting down");

    /* Clean shutdown */
    if (saved_stacking) {
        stacking_free(saved_stacking);
        saved_stacking = NULL;
    }
    if (ctx.windows) {
        windows_list_free(ctx.windows);
        ctx.windows = NULL;
    }
    overlay_destroy(&ctx);
    hotkey_ungrab(&ctx);

    /* Only close display if it's still valid */
    if (display_is_valid(ctx.display)) {
        XCloseDisplay(ctx.display);
    }

    log_info("Clean shutdown complete");
    return 0;
}
