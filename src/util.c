/*
 * alttab - Alt+Tab window switcher
 * util.c - Utility functions and error handling
 */

#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/* ============================================================
 * Logging with Verbosity Levels
 * ============================================================ */

/* Global log level, default to showing warnings and above */
LogLevel g_log_level = LOG_LEVEL_WARNING;

void log_set_level(LogLevel level)
{
    g_log_level = level;
}

void log_error(const char *fmt, ...)
{
    if (g_log_level < LOG_LEVEL_ERROR)
        return;

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void log_warning(const char *fmt, ...)
{
    if (g_log_level < LOG_LEVEL_WARNING)
        return;

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "warning: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void log_info(const char *fmt, ...)
{
    if (g_log_level < LOG_LEVEL_INFO)
        return;

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "info: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void log_debug(const char *fmt, ...)
{
    if (g_log_level < LOG_LEVEL_DEBUG)
        return;

    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "debug: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

/* ============================================================
 * X11 Error Handling
 * ============================================================ */

/* State for tracking X errors */
static struct {
    Display *display;
    int last_error_code;
    unsigned char last_request;
    unsigned char last_minor;
    char error_text[256];
    bool error_occurred;
} xerror_state = {0};

/*
 * X error handler that captures errors without crashing.
 * This allows us to gracefully handle errors like BadWindow when
 * a window is destroyed while we're trying to access it.
 */
static int xerror_handler(Display *display, XErrorEvent *event)
{
    xerror_state.last_error_code = event->error_code;
    xerror_state.last_request = event->request_code;
    xerror_state.last_minor = event->minor_code;
    xerror_state.error_occurred = true;

    /* Get human-readable error text */
    XGetErrorText(display, event->error_code,
                  xerror_state.error_text, sizeof(xerror_state.error_text));

    /* Log the error at debug level - it may be expected */
    log_debug("X error: %s (code=%d, request=%d, minor=%d)",
              xerror_state.error_text,
              event->error_code,
              event->request_code,
              event->minor_code);

    return 0; /* Never returns in synchronous mode, but signature requires it */
}

void xerror_install_handler(Display *display)
{
    xerror_state.display = display;
    xerror_state.error_occurred = false;
    xerror_state.last_error_code = 0;
    XSetErrorHandler(xerror_handler);
    log_debug("X error handler installed");
}

void xerror_clear(void)
{
    xerror_state.error_occurred = false;
    xerror_state.last_error_code = 0;
    xerror_state.error_text[0] = '\0';
}

int xerror_check(void)
{
    return xerror_state.error_occurred ? xerror_state.last_error_code : 0;
}

const char *xerror_get_text(void)
{
    if (xerror_state.error_occurred && xerror_state.error_text[0] != '\0')
        return xerror_state.error_text;
    return "No error";
}

bool xerror_sync_check(Display *display)
{
    xerror_clear();
    XSync(display, False);
    return !xerror_state.error_occurred;
}

/* ============================================================
 * Display Connection Handling
 * ============================================================ */

/* Callback for when display connection is lost */
static DisplayDisconnectCallback disconnect_callback = NULL;
static void *disconnect_user_data = NULL;

void set_display_disconnect_callback(DisplayDisconnectCallback cb, void *user_data)
{
    disconnect_callback = cb;
    disconnect_user_data = user_data;
}

/*
 * X I/O error handler - called when the display connection is lost.
 * This is a fatal error; we cannot recover but can clean up gracefully.
 * Note: This function must not return (X will call exit after it).
 */
static int xioerror_handler(Display *display)
{
    (void)display;
    log_error("Fatal: X display connection lost");

    if (disconnect_callback) {
        disconnect_callback(disconnect_user_data);
    }

    /* Exit cleanly - we cannot continue without a display */
    exit(1);
}

bool display_is_valid(Display *display)
{
    if (!display)
        return false;

    /* Try a simple X operation to check if connection is alive */
    xerror_clear();
    XSync(display, False);

    /* If we got here without I/O error, connection is valid */
    return true;
}

/*
 * Call this after xerror_install_handler to also handle I/O errors.
 * Must be called separately as it installs a different handler.
 */
void xioerror_install_handler(Display *display)
{
    (void)display;
    XSetIOErrorHandler(xioerror_handler);
    log_debug("X I/O error handler installed");
}
