/* SPDX-License-Identifier: MIT */
/*
 * asc - A Shortcut Creator
 * state.h - shared constants, tables and application state.
 * Pure data: no SDL, no Nuklear, no I/O.
 */
#ifndef ASC_STATE_H
#define ASC_STATE_H

#define MAX_PATH        1024
#define MAX_NAME        256
#define MAX_PROTON      32
#define MAX_LOCALE      64
#define MAX_CATEGORIES  32   /* upper bound for stack arrays */

/* WoW64 environment variable used by umu/Proton */
#define WOW64_FLAG     "PROTON_USE_WINE64=1"
/* Wayland support flag */
#define WAYLAND_FLAG   "PROTON_ENABLE_WAYLAND=1"

/* XFCE menu categories - display name + .desktop value */
typedef struct { const char *label; const char *value; } Category;

/* Defined in core.c */
extern const Category    CATEGORIES[];
extern const int         NUM_CATEGORIES;
extern const char *const LOCALE_PRESETS[];
extern const int         NUM_LOCALE_PRESETS;

/* Native / AppImage state */
typedef struct {
    char name[MAX_NAME];
    char bin_path[MAX_PATH];
    char icon_path[MAX_PATH];
    int  category_index;
    char custom_category[MAX_NAME];

    char status_msg[2048];
    int  status_ok;
} NativeState;

/* Wine / umu state */
typedef struct {
    char name[MAX_NAME];           /* Game display name */
    char exe_path[MAX_PATH];       /* Path to Windows .exe */
    char icon_path[MAX_PATH];      /* Source icon path */
    char custom_proton[MAX_PATH];  /* Custom proton path (if selected) */
    char wineprefix[MAX_PATH];     /* WINEPREFIX path (optional) */
    int  proton_index;             /* Index in proton_list, last = custom */
    int  locale_index;             /* Index in LOCALE_PRESETS */
    char custom_locale[MAX_LOCALE];/* Custom locale string */
    int  wow64;                    /* WoW64 checkbox state */
    int  wayland;                  /* Wayland checkbox state */
    int  category_index;           /* Category for .desktop (Wine tab) */
    char custom_category[MAX_NAME];/* Custom category string */

    /* Detected Proton versions */
    char proton_list[MAX_PROTON][MAX_PATH];
    char proton_labels[MAX_PROTON][MAX_NAME];
    int  proton_count;

    /* Status message shown after creation */
    char status_msg[2048];
    int  status_ok;                /* 1 = success, 0 = error */
} AppState;

#endif /* ASC_STATE_H */
