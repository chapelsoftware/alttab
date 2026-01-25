/*
 * alttab - Alt+Tab window switcher
 * hotkey.h - Hotkey capture with XGrabKey
 */

#ifndef ALTTAB_HOTKEY_H
#define ALTTAB_HOTKEY_H

#include "main.h"

/*
 * Grab Alt+Tab hotkey on the root window.
 * Handles all lock key variations (CapsLock, NumLock, ScrollLock).
 * Also grabs ISO_Left_Tab for Shift+Tab support.
 * Returns true on success (or partial success with warning).
 */
bool hotkey_grab(AppContext *ctx);

/*
 * Release all grabbed hotkeys.
 */
void hotkey_ungrab(AppContext *ctx);

/*
 * Check if Alt key has been released using XQueryKeymap.
 * Returns true if neither left nor right Alt is currently pressed.
 */
bool hotkey_alt_released(AppContext *ctx);

#endif /* ALTTAB_HOTKEY_H */
