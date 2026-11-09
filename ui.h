/* SPDX-License-Identifier: MIT */
/*
 * asc - A Shortcut Creator
 * ui.h - Public GTK UI interface.
 * Declares the functions used to build the application window.
 */
#ifndef ASC_UI_H
#define ASC_UI_H

#include <gtk/gtk.h>
#include "state.h"

/* Build and return the top-level GtkApplicationWindow.
 * Called once from the GApplication::activate signal handler. */
GtkWidget *ui_build (GtkApplication *app, AppState *s, NativeState *n);

#endif /* ASC_UI_H */
