/*
 * test_overlay.c - Test the overlay window
 *
 * Compile: gcc -o test_overlay tests/test_overlay.c src/overlay.c src/ewmh.c src/util.c \
 *          $(pkg-config --cflags --libs x11 xcomposite xrender cairo)
 * Run: ./test_overlay
 */

#include <unistd.h>
#include "../src/main.h"
#include "../src/overlay.h"

int main(void)
{
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

    /* Create overlay */
    if (!overlay_create(&ctx)) {
        log_error("Failed to create overlay");
        XCloseDisplay(ctx.display);
        return 1;
    }

    log_debug("Test: showing overlay for 3 seconds...");

    /* Show overlay */
    overlay_show(&ctx);
    overlay_draw(&ctx);

    /* Wait 3 seconds */
    sleep(3);

    /* Hide overlay */
    log_debug("Test: hiding overlay for 1 second...");
    overlay_hide(&ctx);
    sleep(1);

    /* Show again to test repeated show/hide */
    log_debug("Test: showing overlay again for 2 seconds...");
    overlay_show(&ctx);
    overlay_draw(&ctx);
    sleep(2);

    /* Clean up */
    overlay_destroy(&ctx);
    XCloseDisplay(ctx.display);

    log_debug("Test completed successfully!");
    return 0;
}
