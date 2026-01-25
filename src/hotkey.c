/*
 * alttab - Alt+Tab window switcher
 * hotkey.c - Hotkey capture with XGrabKey
 */

#include "main.h"
#include "util.h"
#include <X11/keysym.h>

/* Lock key modifier masks */
#define CAPSLOCK_MASK   LockMask
#define NUMLOCK_MASK    Mod2Mask
#define SCROLLLOCK_MASK Mod3Mask

/* All combinations of lock modifiers we need to grab */
static const unsigned int lock_masks[] = {
    0,
    CAPSLOCK_MASK,
    NUMLOCK_MASK,
    SCROLLLOCK_MASK,
    CAPSLOCK_MASK | NUMLOCK_MASK,
    CAPSLOCK_MASK | SCROLLLOCK_MASK,
    NUMLOCK_MASK | SCROLLLOCK_MASK,
    CAPSLOCK_MASK | NUMLOCK_MASK | SCROLLLOCK_MASK
};
static const int num_lock_masks = sizeof(lock_masks) / sizeof(lock_masks[0]);

/* Error handler state for detecting BadAccess */
static bool grab_error_occurred = false;

static int grab_error_handler(Display *display, XErrorEvent *event)
{
    (void)display;
    if (event->error_code == BadAccess) {
        grab_error_occurred = true;
    }
    return 0;
}

static bool grab_key_with_locks(AppContext *ctx, KeyCode keycode, unsigned int base_mod)
{
    for (int i = 0; i < num_lock_masks; i++) {
        unsigned int mod = base_mod | lock_masks[i];
        XGrabKey(ctx->display, keycode, mod, ctx->root,
                 True, GrabModeAsync, GrabModeAsync);
    }
    return true;
}

static void ungrab_key_with_locks(AppContext *ctx, KeyCode keycode, unsigned int base_mod)
{
    for (int i = 0; i < num_lock_masks; i++) {
        unsigned int mod = base_mod | lock_masks[i];
        XUngrabKey(ctx->display, keycode, mod, ctx->root);
    }
}

bool hotkey_grab(AppContext *ctx)
{
    KeyCode tab_keycode;
    KeyCode iso_left_tab_keycode;
    int (*old_handler)(Display *, XErrorEvent *);

    /* Get keycodes */
    tab_keycode = XKeysymToKeycode(ctx->display, XK_Tab);
    iso_left_tab_keycode = XKeysymToKeycode(ctx->display, XK_ISO_Left_Tab);

    if (tab_keycode == 0) {
        log_error("Cannot get keycode for Tab key");
        return false;
    }

    /* Install error handler to catch BadAccess */
    grab_error_occurred = false;
    XSync(ctx->display, False);
    old_handler = XSetErrorHandler(grab_error_handler);

    /* Grab Alt+Tab (Mod1Mask = Alt) */
    grab_key_with_locks(ctx, tab_keycode, Mod1Mask);

    /* Grab Alt+Shift+Tab */
    grab_key_with_locks(ctx, tab_keycode, Mod1Mask | ShiftMask);

    /* Also grab ISO_Left_Tab for Shift+Tab on some keyboards */
    if (iso_left_tab_keycode != 0 && iso_left_tab_keycode != tab_keycode) {
        grab_key_with_locks(ctx, iso_left_tab_keycode, Mod1Mask);
        grab_key_with_locks(ctx, iso_left_tab_keycode, Mod1Mask | ShiftMask);
    }

    /* Sync and restore error handler */
    XSync(ctx->display, False);
    XSetErrorHandler(old_handler);

    if (grab_error_occurred) {
        log_warning("Alt+Tab may already be grabbed by another application");
        /* Continue anyway - partial grab might still work */
    }

    return true;
}

void hotkey_ungrab(AppContext *ctx)
{
    KeyCode tab_keycode;
    KeyCode iso_left_tab_keycode;

    tab_keycode = XKeysymToKeycode(ctx->display, XK_Tab);
    iso_left_tab_keycode = XKeysymToKeycode(ctx->display, XK_ISO_Left_Tab);

    if (tab_keycode != 0) {
        ungrab_key_with_locks(ctx, tab_keycode, Mod1Mask);
        ungrab_key_with_locks(ctx, tab_keycode, Mod1Mask | ShiftMask);
    }

    if (iso_left_tab_keycode != 0 && iso_left_tab_keycode != tab_keycode) {
        ungrab_key_with_locks(ctx, iso_left_tab_keycode, Mod1Mask);
        ungrab_key_with_locks(ctx, iso_left_tab_keycode, Mod1Mask | ShiftMask);
    }

    XSync(ctx->display, False);
}

bool hotkey_alt_released(AppContext *ctx)
{
    char keymap[32];
    KeyCode alt_l_keycode;
    KeyCode alt_r_keycode;
    int alt_l_byte, alt_l_bit;
    int alt_r_byte, alt_r_bit;
    bool alt_l_pressed = false;
    bool alt_r_pressed = false;

    /* Query current keyboard state */
    XQueryKeymap(ctx->display, keymap);

    /* Check left Alt */
    alt_l_keycode = XKeysymToKeycode(ctx->display, XK_Alt_L);
    if (alt_l_keycode != 0) {
        alt_l_byte = alt_l_keycode / 8;
        alt_l_bit = alt_l_keycode % 8;
        alt_l_pressed = (keymap[alt_l_byte] & (1 << alt_l_bit)) != 0;
    }

    /* Check right Alt */
    alt_r_keycode = XKeysymToKeycode(ctx->display, XK_Alt_R);
    if (alt_r_keycode != 0) {
        alt_r_byte = alt_r_keycode / 8;
        alt_r_bit = alt_r_keycode % 8;
        alt_r_pressed = (keymap[alt_r_byte] & (1 << alt_r_bit)) != 0;
    }

    /* Alt is released if neither left nor right Alt is pressed */
    return !alt_l_pressed && !alt_r_pressed;
}
