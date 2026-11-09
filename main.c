/* SPDX-License-Identifier: MIT */
/*
 * asc - A Shortcut Creator
 * main.c - GtkApplication entry point.
 * No SDL, no OpenGL, no picker thread.
 */
#include "state.h"
#include "core.h"
#include "ui.h"

#include <gtk/gtk.h>
#include <string.h>

typedef struct {
    AppState    s;
    NativeState n;
} AppCtx;

static void
on_activate (GtkApplication *app, gpointer data)
{
    AppCtx *ctx = data;
    detect_proton (&ctx->s);
    ui_build (app, &ctx->s, &ctx->n);
}

int
main (int argc, char *argv[])
{
    AppCtx ctx;
    memset (&ctx, 0, sizeof (ctx));

    g_autoptr (GtkApplication) app = gtk_application_new (
        "io.github.asc", G_APPLICATION_DEFAULT_FLAGS);

    g_signal_connect (app, "activate", G_CALLBACK (on_activate), &ctx);

    int status = g_application_run (G_APPLICATION (app), argc, argv);
    return status;
}
