/*
 * alttab - Alt+Tab window switcher
 * windows.h - Window enumeration and filtering
 */

#ifndef ALTTAB_WINDOWS_H
#define ALTTAB_WINDOWS_H

#include "main.h"

/*
 * MRU (Most Recently Used) list for tracking window focus order.
 * Uses a doubly-linked list for O(1) move-to-front operations
 * and a hash table for O(1) lookup by window ID.
 */

/* Hash table size - should be prime for better distribution */
#define MRU_HASH_SIZE 127

/* Node in the MRU doubly-linked list */
typedef struct MRUNode {
    Window window;
    struct MRUNode *prev;
    struct MRUNode *next;
    struct MRUNode *hash_next;  /* For hash table chaining */
} MRUNode;

/* MRU list with hash table for O(1) lookup */
typedef struct MRUList {
    MRUNode *head;              /* Most recently used */
    MRUNode *tail;              /* Least recently used */
    MRUNode *hash_table[MRU_HASH_SIZE];
    int count;
} MRUList;

/* MRU list functions */
MRUList *mru_init(void);
void mru_free(MRUList *list);
void mru_touch(MRUList *list, Window win);
void mru_remove(MRUList *list, Window win);
Window *mru_get_order(MRUList *list, int *count);
void mru_init_from_stacking(MRUList *list, AppContext *ctx);

/* ============================================================
 * Window Stacking Order Save/Restore
 * ============================================================ */

/* Saved stacking order for cancel/restore functionality */
typedef struct StackingOrder {
    Window *windows;        /* Array of window IDs in stacking order (bottom to top) */
    int count;              /* Number of windows */
} StackingOrder;

/* Stacking order functions */
StackingOrder *stacking_save(AppContext *ctx);
void stacking_restore(AppContext *ctx, StackingOrder *order);
void stacking_free(StackingOrder *order);

/* Preview window raising */
void raise_preview_window(AppContext *ctx, Window win, Window overlay_window);

#endif /* ALTTAB_WINDOWS_H */
