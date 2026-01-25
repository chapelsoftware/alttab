/*
 * alttab - Alt+Tab window switcher
 * main.c - Entry point and main event loop
 */

#include "main.h"

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    AppContext ctx = {0};

    /* Open X display */
    ctx.display = XOpenDisplay(NULL);
    if (!ctx.display) {
        log_error("Cannot open X display");
        return 1;
    }

    ctx.screen = DefaultScreen(ctx.display);
    ctx.root = RootWindow(ctx.display, ctx.screen);

    /* Initialize EWMH atoms */
    ewmh_init_atoms(&ctx);

    /* Grab hotkey */
    if (!hotkey_grab(&ctx)) {
        log_error("Failed to grab hotkey");
        XCloseDisplay(ctx.display);
        return 1;
    }

    /* TODO: Event loop will go here */

    /* Clean shutdown */
    hotkey_ungrab(&ctx);
    XCloseDisplay(ctx.display);

    return 0;
}
