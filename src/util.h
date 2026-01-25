/*
 * alttab - Alt+Tab window switcher
 * util.h - Utility functions and error handling
 */

#ifndef ALTTAB_UTIL_H
#define ALTTAB_UTIL_H

#include <stdbool.h>
#include <X11/Xlib.h>

/* Verbosity levels for logging */
typedef enum {
    LOG_LEVEL_QUIET = 0,   /* No output */
    LOG_LEVEL_ERROR = 1,   /* Errors only */
    LOG_LEVEL_WARNING = 2, /* Errors and warnings */
    LOG_LEVEL_INFO = 3,    /* Normal operation messages */
    LOG_LEVEL_DEBUG = 4    /* Verbose debug output */
} LogLevel;

/* Global verbosity level (default: LOG_LEVEL_WARNING) */
extern LogLevel g_log_level;

/* Logging functions with verbosity control */
void log_error(const char *fmt, ...);
void log_warning(const char *fmt, ...);
void log_info(const char *fmt, ...);
void log_debug(const char *fmt, ...);

/* Set the log level */
void log_set_level(LogLevel level);

/*
 * Global X11 error handling
 *
 * The error handler tracks the last error that occurred, allowing code
 * to check for errors after X calls. Use xerror_check() after X operations
 * that might fail (e.g., accessing destroyed windows).
 */

/* Install the global X error handler. Call once at startup. */
void xerror_install_handler(Display *display);

/* Clear the error flag before an operation */
void xerror_clear(void);

/* Check if an error occurred since last clear. Returns error code or 0. */
int xerror_check(void);

/* Get human-readable error text for the last error */
const char *xerror_get_text(void);

/* Execute an X operation with error checking.
 * Returns true if no error occurred. */
bool xerror_sync_check(Display *display);

/*
 * Safe wrapper for X operations that might fail on destroyed windows.
 * Clears error state, performs the operation, syncs and checks for errors.
 */
#define XERROR_SAFE(display, operation) \
    do { \
        xerror_clear(); \
        operation; \
        XSync(display, False); \
    } while(0)

/* Check if display connection is still valid */
bool display_is_valid(Display *display);

/* I/O error handler for fatal display disconnect */
typedef void (*DisplayDisconnectCallback)(void *user_data);
void set_display_disconnect_callback(DisplayDisconnectCallback cb, void *user_data);

/* Install I/O error handler for display disconnection. Call after xerror_install_handler. */
void xioerror_install_handler(Display *display);

#endif /* ALTTAB_UTIL_H */
