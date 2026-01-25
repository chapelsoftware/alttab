/*
 * alttab - Alt+Tab window switcher
 * util.c - Utility functions
 */

#include "main.h"
#include <stdarg.h>

void log_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "error: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void log_warning(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "warning: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}

void log_debug(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fprintf(stderr, "debug: ");
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    va_end(args);
}
