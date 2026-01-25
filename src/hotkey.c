/*
 * alttab - Alt+Tab window switcher
 * hotkey.c - Hotkey capture with XGrabKey
 */

#include "main.h"
#include "config.h"
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
    KeyCode key_keycode;
    KeyCode iso_left_tab_keycode;
    int (*old_handler)(Display *, XErrorEvent *);
    unsigned int mod_mask = g_config.mod_mask;
    KeySym key_sym = g_config.key_sym;

    /* Get keycodes from configured key */
    key_keycode = XKeysymToKeycode(ctx->display, key_sym);
    iso_left_tab_keycode = XKeysymToKeycode(ctx->display, XK_ISO_Left_Tab);

    if (key_keycode == 0) {
        log_error("Cannot get keycode for key: %s", g_config.key_name);
        return false;
    }

    /* Install error handler to catch BadAccess */
    grab_error_occurred = false;
    XSync(ctx->display, False);
    old_handler = XSetErrorHandler(grab_error_handler);

    /* Grab modifier+key */
    grab_key_with_locks(ctx, key_keycode, mod_mask);

    /* Grab modifier+Shift+key */
    grab_key_with_locks(ctx, key_keycode, mod_mask | ShiftMask);

    /* Also grab ISO_Left_Tab for Shift+Tab on some keyboards (only if Tab is the key) */
    if (key_sym == XK_Tab && iso_left_tab_keycode != 0 &&
        iso_left_tab_keycode != key_keycode) {
        grab_key_with_locks(ctx, iso_left_tab_keycode, mod_mask);
        grab_key_with_locks(ctx, iso_left_tab_keycode, mod_mask | ShiftMask);
    }

    /* Sync and restore error handler */
    XSync(ctx->display, False);
    XSetErrorHandler(old_handler);

    if (grab_error_occurred) {
        log_warning("%s+%s may already be grabbed by another application",
                    g_config.mod_name, g_config.key_name);
        /* Continue anyway - partial grab might still work */
    }

    log_info("Grabbed hotkey: %s+%s", g_config.mod_name, g_config.key_name);
    return true;
}

void hotkey_ungrab(AppContext *ctx)
{
    KeyCode key_keycode;
    KeyCode iso_left_tab_keycode;
    unsigned int mod_mask = g_config.mod_mask;
    KeySym key_sym = g_config.key_sym;

    key_keycode = XKeysymToKeycode(ctx->display, key_sym);
    iso_left_tab_keycode = XKeysymToKeycode(ctx->display, XK_ISO_Left_Tab);

    if (key_keycode != 0) {
        ungrab_key_with_locks(ctx, key_keycode, mod_mask);
        ungrab_key_with_locks(ctx, key_keycode, mod_mask | ShiftMask);
    }

    if (key_sym == XK_Tab && iso_left_tab_keycode != 0 &&
        iso_left_tab_keycode != key_keycode) {
        ungrab_key_with_locks(ctx, iso_left_tab_keycode, mod_mask);
        ungrab_key_with_locks(ctx, iso_left_tab_keycode, mod_mask | ShiftMask);
    }

    XSync(ctx->display, False);
}

/*
 * Check if the configured modifier key has been released.
 * Returns true when the modifier is no longer pressed.
 */
bool hotkey_alt_released(AppContext *ctx)
{
    char keymap[32];
    KeyCode mod_l_keycode = 0;
    KeyCode mod_r_keycode = 0;
    int mod_l_byte, mod_l_bit;
    int mod_r_byte, mod_r_bit;
    bool mod_l_pressed = false;
    bool mod_r_pressed = false;

    /* Query current keyboard state */
    XQueryKeymap(ctx->display, keymap);

    /* Determine which keys to check based on configured modifier */
    unsigned int mod_mask = g_config.mod_mask;

    if (mod_mask == Mod1Mask) {
        /* Alt keys */
        mod_l_keycode = XKeysymToKeycode(ctx->display, XK_Alt_L);
        mod_r_keycode = XKeysymToKeycode(ctx->display, XK_Alt_R);
    } else if (mod_mask == Mod4Mask) {
        /* Super/Windows keys */
        mod_l_keycode = XKeysymToKeycode(ctx->display, XK_Super_L);
        mod_r_keycode = XKeysymToKeycode(ctx->display, XK_Super_R);
    } else if (mod_mask == ControlMask) {
        /* Control keys */
        mod_l_keycode = XKeysymToKeycode(ctx->display, XK_Control_L);
        mod_r_keycode = XKeysymToKeycode(ctx->display, XK_Control_R);
    } else if (mod_mask == ShiftMask) {
        /* Shift keys */
        mod_l_keycode = XKeysymToKeycode(ctx->display, XK_Shift_L);
        mod_r_keycode = XKeysymToKeycode(ctx->display, XK_Shift_R);
    } else {
        /* Default to Alt for other modifiers (Mod2, Mod3, Mod5) */
        mod_l_keycode = XKeysymToKeycode(ctx->display, XK_Alt_L);
        mod_r_keycode = XKeysymToKeycode(ctx->display, XK_Alt_R);
    }

    /* Check left modifier key */
    if (mod_l_keycode != 0) {
        mod_l_byte = mod_l_keycode / 8;
        mod_l_bit = mod_l_keycode % 8;
        mod_l_pressed = (keymap[mod_l_byte] & (1 << mod_l_bit)) != 0;
    }

    /* Check right modifier key */
    if (mod_r_keycode != 0) {
        mod_r_byte = mod_r_keycode / 8;
        mod_r_bit = mod_r_keycode % 8;
        mod_r_pressed = (keymap[mod_r_byte] & (1 << mod_r_bit)) != 0;
    }

    /* Modifier is released if neither left nor right key is pressed */
    return !mod_l_pressed && !mod_r_pressed;
}
