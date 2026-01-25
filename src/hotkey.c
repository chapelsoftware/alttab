/*
 * alttab - Alt+Tab window switcher
 * hotkey.c - Hotkey capture with XGrabKey
 */

#include "main.h"

bool hotkey_grab(AppContext *ctx)
{
    (void)ctx;
    return true;
}

void hotkey_ungrab(AppContext *ctx)
{
    (void)ctx;
}

bool hotkey_alt_released(AppContext *ctx)
{
    (void)ctx;
    return false;
}
