/* SPDX-License-Identifier: MIT */
/*
 * asc - A Shortcut Creator
 * ui.c - GTK4 UI implementation: widget tree,
 * dialogs, signal handlers, and UI helpers.
 */
#include "ui.h"
#include "core.h"

#include <gtk/gtk.h>
#include <string.h>

/* ─── Per-tab widget bundles ─────────────────────────────────
 * Collected into structs so signal callbacks can reach the
 * widgets they need without globals.                          */

typedef struct {
    AppState  *s;

    GtkWidget *name;
    GtkWidget *category;
    GtkWidget *custom_category;
    GtkWidget *custom_category_row; /* revealer */
    GtkWidget *exe_path;
    GtkWidget *proton;
    GtkWidget *custom_proton;
    GtkWidget *custom_proton_row;   /* revealer */
    GtkWidget *locale;
    GtkWidget *custom_locale;
    GtkWidget *custom_locale_row;   /* revealer */
    GtkWidget *icon_path;
    GtkWidget *wineprefix;
    GtkWidget *wow64;
    GtkWidget *wayland;
    GtkWidget *status;
} WineWidgets;

typedef struct {
    NativeState *n;

    GtkWidget *name;
    GtkWidget *category;
    GtkWidget *custom_category;
    GtkWidget *custom_category_row; /* revealer */
    GtkWidget *bin_path;
    GtkWidget *icon_path;
    GtkWidget *status;
} NativeWidgets;

/* ─── Helpers ────────────────────────────────────────────────── */

/* Read a GtkEntry into a fixed C buffer */
static void
entry_to_buf (GtkWidget *entry, char *buf, size_t bufsz)
{
    const char *txt = gtk_editable_get_text (GTK_EDITABLE (entry));
    snprintf (buf, bufsz, "%s", txt != NULL ? txt : "");
}

/* Show a coloured status label */
static void
show_status (GtkWidget *label, const char *msg, int ok)
{
    gtk_label_set_text (GTK_LABEL (label), msg);
    /* Remove previous color class */
    gtk_widget_remove_css_class (label, "status-ok");
    gtk_widget_remove_css_class (label, "status-err");
    gtk_widget_add_css_class (label, ok ? "status-ok" : "status-err");
    gtk_widget_set_visible (label, TRUE);
}

static void
free_signal_data (gpointer data, GClosure *closure)
{
    (void)closure;
    g_free (data);
}

/* Labelled entry row: returns the GtkEntry */
static GtkWidget *
labeled_entry (GtkWidget *box, const char *label_text)
{
    GtkWidget *lbl = gtk_label_new (label_text);
    gtk_label_set_xalign (GTK_LABEL (lbl), 0.0f);
    gtk_widget_add_css_class (lbl, "dim-label");
    gtk_box_append (GTK_BOX (box), lbl);

    GtkWidget *entry = gtk_entry_new ();
    gtk_box_append (GTK_BOX (box), entry);
    return entry;
}

/* Entry + Browse button row: returns the GtkEntry */
static GtkWidget *
browse_row (GtkWidget *box,
            const char *label_text,
            GCallback   browse_cb,
            gpointer    cb_data)
{
    GtkWidget *lbl = gtk_label_new (label_text);
    gtk_label_set_xalign (GTK_LABEL (lbl), 0.0f);
    gtk_widget_add_css_class (lbl, "dim-label");
    gtk_box_append (GTK_BOX (box), lbl);

    GtkWidget *row = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *entry = gtk_entry_new ();
    gtk_widget_set_hexpand (entry, TRUE);
    GtkWidget *btn = gtk_button_new_with_label ("Browse…");
    gtk_box_append (GTK_BOX (row), entry);
    gtk_box_append (GTK_BOX (row), btn);
    gtk_box_append (GTK_BOX (box), row);

    g_signal_connect_data (btn, "clicked", browse_cb, cb_data,
                           free_signal_data, 0);
    return entry;
}

/* GtkStringList from a C string array */
static GtkStringList *
strlist (const char **items, int n)
{
    GtkStringList *sl = gtk_string_list_new (NULL);
    for (int i = 0; i < n; i++)
        gtk_string_list_append (sl, items[i]);
    return sl;
}

/* Dropdown from a C string array */
static GtkWidget *
make_dropdown (const char **items, int n)
{
    GtkStringList *sl = strlist (items, n);
    return gtk_drop_down_new (G_LIST_MODEL (sl), NULL);
}

/* ─── File dialog callbacks ──────────────────────────────────── */

/* Generic finish helper: copy chosen file path into a GtkEntry */
static void
file_dialog_finish (GObject       *src,
                    GAsyncResult  *res,
                    gpointer      entry)
{
    GtkFileDialog *dlg = GTK_FILE_DIALOG (src);
    g_autoptr (GFile) file = gtk_file_dialog_open_finish (dlg, res, NULL);
    if (file == NULL)
        return;

    g_autofree char *path = g_file_get_path (file);
    if (path != NULL)
        gtk_editable_set_text (GTK_EDITABLE (entry), path);
}

static void
folder_dialog_finish (GObject       *src,
                      GAsyncResult  *res,
                      gpointer      entry)
{
    GtkFileDialog *dlg = GTK_FILE_DIALOG (src);
    g_autoptr (GFile) file = gtk_file_dialog_select_folder_finish (dlg, res, NULL);
    if (file == NULL)
        return;

    g_autofree char *path = g_file_get_path (file);
    if (path != NULL)
        gtk_editable_set_text (GTK_EDITABLE (entry), path);
}

/* Build a GtkFileDialog with an optional single filter */
static GtkFileDialog *
make_file_dialog (const char *title,
                  const char *filter_name,
                  const char *mime_or_glob)
{
    GtkFileDialog *dlg = gtk_file_dialog_new ();
    gtk_file_dialog_set_title (dlg, title);
    gtk_file_dialog_set_modal (dlg, TRUE);

    if (filter_name != NULL && mime_or_glob != NULL) {
        g_autoptr (GtkFileFilter) ff = gtk_file_filter_new ();
        gtk_file_filter_set_name (ff, filter_name);
        /* Treat as glob pattern */
        gtk_file_filter_add_pattern (ff, mime_or_glob);

        g_autoptr (GListStore) filters = g_list_store_new (GTK_TYPE_FILE_FILTER);
        g_list_store_append (filters, ff);
        gtk_file_dialog_set_filters (dlg, G_LIST_MODEL (filters));
        gtk_file_dialog_set_default_filter (dlg, ff);
    }

    return dlg;
}

/* ── Wine tab browse callbacks ── */

typedef struct { GtkWidget *entry; GtkWindow *win; } BrowseCbData;

static void
on_browse_exe (GtkButton *btn, gpointer data)
{
    (void)btn;
    BrowseCbData *d = data;
    g_autoptr (GtkFileDialog) dlg = make_file_dialog (
        "Select Game Executable", "Windows Executables", "*.exe");
    gtk_file_dialog_open (dlg, d->win, NULL, file_dialog_finish, d->entry);
}

static void
on_browse_icon (GtkButton *btn, gpointer data)
{
    (void)btn;
    BrowseCbData *d = data;
    g_autoptr (GtkFileDialog) dlg = make_file_dialog (
        "Select Icon", "Images", "*.png");
    gtk_file_dialog_open (dlg, d->win, NULL, file_dialog_finish, d->entry);
}

static void
on_browse_proton (GtkButton *btn, gpointer data)
{
    (void)btn;
    BrowseCbData *d = data;
    g_autoptr (GtkFileDialog) dlg = make_file_dialog (
        "Select Proton Directory", NULL, NULL);
    gtk_file_dialog_select_folder (dlg, d->win, NULL,
                                   folder_dialog_finish, d->entry);
}

static void
on_browse_wineprefix (GtkButton *btn, gpointer data)
{
    (void)btn;
    BrowseCbData *d = data;
    g_autoptr (GtkFileDialog) dlg = make_file_dialog (
        "Select WINEPREFIX Directory", NULL, NULL);
    gtk_file_dialog_select_folder (dlg, d->win, NULL,
                                   folder_dialog_finish, d->entry);
}

/* ── Native tab browse callbacks ── */

static void
on_browse_bin (GtkButton *btn, gpointer data)
{
    (void)btn;
    BrowseCbData *d = data;
    g_autoptr (GtkFileDialog) dlg = make_file_dialog (
        "Select Binary or AppImage", NULL, NULL);
    gtk_file_dialog_open (dlg, d->win, NULL, file_dialog_finish, d->entry);
}

static void
on_browse_native_icon (GtkButton *btn, gpointer data)
{
    (void)btn;
    BrowseCbData *d = data;
    g_autoptr (GtkFileDialog) dlg = make_file_dialog (
        "Select Icon", "Images", "*.png");
    gtk_file_dialog_open (dlg, d->win, NULL, file_dialog_finish, d->entry);
}

/* ─── Dropdown reveal helpers ────────────────────────────────── */

typedef struct {
    GtkWidget *revealer;
    int        custom_index; /* index that triggers reveal */
} RevealData;

static void
on_dropdown_changed (GObject *obj, GParamSpec *ps, gpointer data)
{
    (void)ps;
    RevealData *rd = data;
    guint sel = gtk_drop_down_get_selected (GTK_DROP_DOWN (obj));
    gtk_revealer_set_reveal_child (GTK_REVEALER (rd->revealer),
                                   (int)sel == rd->custom_index);
}

/* ─── Create Shortcut callbacks ──────────────────────────────── */

static void
sync_wine_state (WineWidgets *w)
{
    AppState *s = w->s;
    entry_to_buf (w->name,           s->name,           sizeof (s->name));
    entry_to_buf (w->exe_path,       s->exe_path,       sizeof (s->exe_path));
    entry_to_buf (w->icon_path,      s->icon_path,      sizeof (s->icon_path));
    entry_to_buf (w->wineprefix,     s->wineprefix,     sizeof (s->wineprefix));
    entry_to_buf (w->custom_proton,  s->custom_proton,  sizeof (s->custom_proton));
    entry_to_buf (w->custom_locale,  s->custom_locale,  sizeof (s->custom_locale));
    entry_to_buf (w->custom_category,s->custom_category,sizeof (s->custom_category));

    s->category_index = (int)gtk_drop_down_get_selected (GTK_DROP_DOWN (w->category));
    s->proton_index   = (int)gtk_drop_down_get_selected (GTK_DROP_DOWN (w->proton));
    s->locale_index   = (int)gtk_drop_down_get_selected (GTK_DROP_DOWN (w->locale));
    s->wow64          = gtk_check_button_get_active (GTK_CHECK_BUTTON (w->wow64));
    s->wayland        = gtk_check_button_get_active (GTK_CHECK_BUTTON (w->wayland));
}

static void
on_create_wine (GtkButton *btn, gpointer data)
{
    (void)btn;
    WineWidgets *w = data;
    sync_wine_state (w);
    create_shortcut (w->s);
    show_status (w->status, w->s->status_msg, w->s->status_ok);
}

static void
sync_native_state (NativeWidgets *w)
{
    NativeState *n = w->n;
    entry_to_buf (w->name,            n->name,            sizeof (n->name));
    entry_to_buf (w->bin_path,        n->bin_path,        sizeof (n->bin_path));
    entry_to_buf (w->icon_path,       n->icon_path,       sizeof (n->icon_path));
    entry_to_buf (w->custom_category, n->custom_category, sizeof (n->custom_category));
    n->category_index = (int)gtk_drop_down_get_selected (
                              GTK_DROP_DOWN (w->category));
}

static void
on_create_native (GtkButton *btn, gpointer data)
{
    (void)btn;
    NativeWidgets *w = data;
    sync_native_state (w);
    create_native_shortcut (w->n);
    show_status (w->status, w->n->status_msg, w->n->status_ok);
}

/* ─── Shortcuts folder button ────────────────────────────────── */

static void
on_open_shortcuts (GtkButton *btn, gpointer win)
{
    (void)btn;
    (void)win;
    const char *home = g_get_home_dir ();
    char path[1024];
    snprintf (path, sizeof (path), "%s/.local/share/applications", home);
    char *argv[] = { "xdg-open", path, NULL };
    g_autoptr (GError) err = NULL;
    g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err);
    if (err != NULL)
        g_printerr ("xdg-open failed: %s\n", err->message);
}

/* ─── CSS ────────────────────────────────────────────────────── */

static void
load_css (void)
{
    g_autoptr (GtkCssProvider) prov = gtk_css_provider_new ();
    gtk_css_provider_load_from_string (
        prov,
        ".dim-label { font-size: 0.85em; opacity: 0.65; margin-top: 6px; }\n"
        ".status-ok  { color: #4caf50; font-weight: bold; }\n"
        ".status-err { color: #f44336; font-weight: bold; }\n"
        ".tab-content { padding: 12px; }\n"
    );
    gtk_style_context_add_provider_for_display (
        gdk_display_get_default (),
        GTK_STYLE_PROVIDER (prov),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

/* ─── Wine / umu tab ─────────────────────────────────────────── */

static GtkWidget *
build_wine_tab (WineWidgets *w, GtkWindow *win)
{
    AppState *s = w->s;

    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class (box, "tab-content");

    /* Game Name */
    w->name = labeled_entry (box, "Game Name");

    /* Category dropdown */
    {
        GtkWidget *lbl = gtk_label_new ("Category");
        gtk_label_set_xalign (GTK_LABEL (lbl), 0.0f);
        gtk_widget_add_css_class (lbl, "dim-label");
        gtk_box_append (GTK_BOX (box), lbl);

        const char *labels[MAX_CATEGORIES];
        for (int i = 0; i < NUM_CATEGORIES && i < MAX_CATEGORIES; i++)
            labels[i] = CATEGORIES[i].label;
        w->category = make_dropdown (labels, NUM_CATEGORIES);
        gtk_drop_down_set_selected (GTK_DROP_DOWN (w->category), 0);
        gtk_box_append (GTK_BOX (box), w->category);

        /* Custom category revealer */
        GtkWidget *rev_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
        w->custom_category = labeled_entry (rev_box, "Custom category name");
        GtkWidget *rev = gtk_revealer_new ();
        gtk_revealer_set_transition_type (GTK_REVEALER (rev),
                                          GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
        gtk_revealer_set_child (GTK_REVEALER (rev), rev_box);
        gtk_box_append (GTK_BOX (box), rev);
        w->custom_category_row = rev;

        RevealData *rd = g_new (RevealData, 1);
        rd->revealer = rev;
        rd->custom_index = NUM_CATEGORIES - 1;
        g_signal_connect_data (w->category, "notify::selected",
                               G_CALLBACK (on_dropdown_changed), rd,
                               free_signal_data, 0);
    }

    /* Executable */
    {
        BrowseCbData *d = g_new (BrowseCbData, 1);
        d->win = win;
        w->exe_path = browse_row (box, "Game Executable (.exe)",
                                  G_CALLBACK (on_browse_exe), d);
        d->entry = w->exe_path;
    }

    /* Proton dropdown */
    {
        GtkWidget *lbl = gtk_label_new ("Proton Version");
        gtk_label_set_xalign (GTK_LABEL (lbl), 0.0f);
        gtk_widget_add_css_class (lbl, "dim-label");
        gtk_box_append (GTK_BOX (box), lbl);

        if (s->proton_count > 0) {
            const char *labels[MAX_PROTON];
            for (int i = 0; i < s->proton_count; i++)
                labels[i] = s->proton_labels[i];
            w->proton = make_dropdown (labels, s->proton_count);
            gtk_drop_down_set_selected (GTK_DROP_DOWN (w->proton), 0);
        } else {
            w->proton = make_dropdown (
                (const char *[]){ "No Proton versions detected", "Custom…" }, 2);
        }
        gtk_box_append (GTK_BOX (box), w->proton);

        /* Custom proton revealer */
        GtkWidget *rev_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
        {
            BrowseCbData *d = g_new (BrowseCbData, 1);
            d->win = win;
            w->custom_proton = browse_row (rev_box, "Proton directory",
                                           G_CALLBACK (on_browse_proton), d);
            d->entry = w->custom_proton;
        }
        GtkWidget *rev = gtk_revealer_new ();
        gtk_revealer_set_transition_type (GTK_REVEALER (rev),
                                          GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
        gtk_revealer_set_child (GTK_REVEALER (rev), rev_box);
        gtk_box_append (GTK_BOX (box), rev);
        w->custom_proton_row = rev;

        RevealData *rd = g_new (RevealData, 1);
        rd->revealer = rev;
        rd->custom_index = s->proton_count - 1;
        g_signal_connect_data (w->proton, "notify::selected",
                               G_CALLBACK (on_dropdown_changed), rd,
                               free_signal_data, 0);
    }

    /* Locale dropdown */
    {
        GtkWidget *lbl = gtk_label_new ("Locale (LANG / LC_ALL)");
        gtk_label_set_xalign (GTK_LABEL (lbl), 0.0f);
        gtk_widget_add_css_class (lbl, "dim-label");
        gtk_box_append (GTK_BOX (box), lbl);

        w->locale = make_dropdown ((const char **)LOCALE_PRESETS,
                                   NUM_LOCALE_PRESETS);
        gtk_drop_down_set_selected (GTK_DROP_DOWN (w->locale), 0);
        gtk_box_append (GTK_BOX (box), w->locale);

        /* Custom locale revealer */
        GtkWidget *rev_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
        w->custom_locale = labeled_entry (rev_box, "Custom locale string");
        GtkWidget *rev = gtk_revealer_new ();
        gtk_revealer_set_transition_type (GTK_REVEALER (rev),
                                          GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
        gtk_revealer_set_child (GTK_REVEALER (rev), rev_box);
        gtk_box_append (GTK_BOX (box), rev);
        w->custom_locale_row = rev;

        RevealData *rd = g_new (RevealData, 1);
        rd->revealer = rev;
        rd->custom_index = NUM_LOCALE_PRESETS - 1;
        g_signal_connect_data (w->locale, "notify::selected",
                               G_CALLBACK (on_dropdown_changed), rd,
                               free_signal_data, 0);
    }

    /* Icon */
    {
        BrowseCbData *d = g_new (BrowseCbData, 1);
        d->win = win;
        w->icon_path = browse_row (box, "Icon (PNG recommended)",
                                   G_CALLBACK (on_browse_icon), d);
        d->entry = w->icon_path;
    }

    /* WINEPREFIX */
    {
        BrowseCbData *d = g_new (BrowseCbData, 1);
        d->win = win;
        w->wineprefix = browse_row (box, "WINEPREFIX (optional)",
                                    G_CALLBACK (on_browse_wineprefix), d);
        d->entry = w->wineprefix;
    }

    /* Checkboxes */
    w->wow64 = gtk_check_button_new_with_label (
        "Enable WoW64 (PROTON_USE_WINE64=1)");
    gtk_box_append (GTK_BOX (box), w->wow64);

    w->wayland = gtk_check_button_new_with_label (
        "Enable Wayland (PROTON_ENABLE_WAYLAND=1)");
    gtk_box_append (GTK_BOX (box), w->wayland);

    /* Separator */
    gtk_box_append (GTK_BOX (box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));

    /* Create Shortcut button */
    GtkWidget *create_btn = gtk_button_new_with_label ("Create Shortcut");
    gtk_widget_add_css_class (create_btn, "suggested-action");
    g_signal_connect (create_btn, "clicked", G_CALLBACK (on_create_wine), w);
    gtk_box_append (GTK_BOX (box), create_btn);

    /* Shortcuts folder button */
    GtkWidget *sc_btn = gtk_button_new_with_label ("Open Shortcuts Folder");
    g_signal_connect (sc_btn, "clicked", G_CALLBACK (on_open_shortcuts), win);
    gtk_box_append (GTK_BOX (box), sc_btn);

    /* Status label */
    w->status = gtk_label_new ("");
    gtk_label_set_wrap (GTK_LABEL (w->status), TRUE);
    gtk_widget_set_visible (w->status, FALSE);
    gtk_box_append (GTK_BOX (box), w->status);

    return box;
}

/* ─── Native / AppImage tab ──────────────────────────────────── */

static GtkWidget *
build_native_tab (NativeWidgets *w, GtkWindow *win)
{
    GtkWidget *box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_add_css_class (box, "tab-content");

    /* App Name */
    w->name = labeled_entry (box, "App Name");

    /* Category dropdown */
    {
        GtkWidget *lbl = gtk_label_new ("Category");
        gtk_label_set_xalign (GTK_LABEL (lbl), 0.0f);
        gtk_widget_add_css_class (lbl, "dim-label");
        gtk_box_append (GTK_BOX (box), lbl);

        const char *labels[MAX_CATEGORIES];
        for (int i = 0; i < NUM_CATEGORIES && i < MAX_CATEGORIES; i++)
            labels[i] = CATEGORIES[i].label;
        w->category = make_dropdown (labels, NUM_CATEGORIES);
        gtk_drop_down_set_selected (GTK_DROP_DOWN (w->category), 0);
        gtk_box_append (GTK_BOX (box), w->category);

        GtkWidget *rev_box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
        w->custom_category = labeled_entry (rev_box, "Custom category name");
        GtkWidget *rev = gtk_revealer_new ();
        gtk_revealer_set_transition_type (GTK_REVEALER (rev),
                                          GTK_REVEALER_TRANSITION_TYPE_SLIDE_DOWN);
        gtk_revealer_set_child (GTK_REVEALER (rev), rev_box);
        gtk_box_append (GTK_BOX (box), rev);
        w->custom_category_row = rev;

        RevealData *rd = g_new (RevealData, 1);
        rd->revealer = rev;
        rd->custom_index = NUM_CATEGORIES - 1;
        g_signal_connect_data (w->category, "notify::selected",
                               G_CALLBACK (on_dropdown_changed), rd,
                               free_signal_data, 0);
    }

    /* Binary / AppImage */
    {
        BrowseCbData *d = g_new (BrowseCbData, 1);
        d->win = win;
        w->bin_path = browse_row (box, "Binary / AppImage",
                                  G_CALLBACK (on_browse_bin), d);
        d->entry = w->bin_path;
    }

    /* Icon */
    {
        BrowseCbData *d = g_new (BrowseCbData, 1);
        d->win = win;
        w->icon_path = browse_row (box, "Icon (PNG recommended)",
                                   G_CALLBACK (on_browse_native_icon), d);
        d->entry = w->icon_path;
    }

    /* Separator */
    gtk_box_append (GTK_BOX (box), gtk_separator_new (GTK_ORIENTATION_HORIZONTAL));

    /* Create Shortcut button */
    GtkWidget *create_btn = gtk_button_new_with_label ("Create Shortcut");
    gtk_widget_add_css_class (create_btn, "suggested-action");
    g_signal_connect (create_btn, "clicked", G_CALLBACK (on_create_native), w);
    gtk_box_append (GTK_BOX (box), create_btn);

    /* Shortcuts folder button */
    GtkWidget *sc_btn = gtk_button_new_with_label ("Open Shortcuts Folder");
    g_signal_connect (sc_btn, "clicked", G_CALLBACK (on_open_shortcuts), win);
    gtk_box_append (GTK_BOX (box), sc_btn);

    /* Status label */
    w->status = gtk_label_new ("");
    gtk_label_set_wrap (GTK_LABEL (w->status), TRUE);
    gtk_widget_set_visible (w->status, FALSE);
    gtk_box_append (GTK_BOX (box), w->status);

    return box;
}

/* ─── Top-level window ───────────────────────────────────────── */

GtkWidget *
ui_build (GtkApplication *app, AppState *s, NativeState *n)
{
    load_css ();

    GtkWidget *win = gtk_application_window_new (app);
    gtk_window_set_title (GTK_WINDOW (win), "asc — A Shortcut Creator");
    gtk_window_set_default_size (GTK_WINDOW (win), 620, 700);

    /* Allocate widget bundles (freed with the window via GObject lifecycle) */
    WineWidgets   *ww = g_new0 (WineWidgets,   1); ww->s = s;
    NativeWidgets *nw = g_new0 (NativeWidgets,  1); nw->n = n;

    GtkWidget *notebook = gtk_notebook_new ();
    gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_TOP);

    gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
                              build_wine_tab (ww, GTK_WINDOW (win)),
                              gtk_label_new ("Wine / umu"));
    gtk_notebook_append_page (GTK_NOTEBOOK (notebook),
                              build_native_tab (nw, GTK_WINDOW (win)),
                              gtk_label_new ("Native / AppImage"));

    gtk_window_set_child (GTK_WINDOW (win), notebook);

    /* Free widget bundles when the window is destroyed */
    g_object_set_data_full (G_OBJECT (win), "wine-widgets",   ww, g_free);
    g_object_set_data_full (G_OBJECT (win), "native-widgets", nw, g_free);

    gtk_window_present (GTK_WINDOW (win));
    return win;
}
