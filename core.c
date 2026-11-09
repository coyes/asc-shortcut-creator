/* SPDX-License-Identifier: MIT */
/*
 * asc - A Shortcut Creator
 * core.c - shortcut creation logic.
 */
#define _XOPEN_SOURCE 700
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "core.h"
#include "util.h"
#include "stb_image.h"
#include "stb_image_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <glib.h>

/* ─── Shared tables (declared extern in state.h) ───────────── */

const Category CATEGORIES[] = {
    { "Games",       "Game"       },
    { "Accessories", "Utility"    },
    { "Development", "Development"},
    { "Education",   "Education"  },
    { "Graphics",    "Graphics"   },
    { "Internet",    "Network"    },
    { "Multimedia",  "AudioVideo" },
    { "Office",      "Office"     },
    { "Science",     "Science"    },
    { "System",      "System"     },
    { "Settings",    "Settings"   },
    { "Other",       "X-Other"    },
    { "Custom...",   ""           },
};
const int NUM_CATEGORIES = (int)(sizeof (CATEGORIES) / sizeof (CATEGORIES[0]));

const char *const LOCALE_PRESETS[] = {
    "None",
    "ja_JP.UTF-8",
    "en_US.UTF-8",
    "zh_CN.UTF-8",
    "zh_TW.UTF-8",
    "ko_KR.UTF-8",
    "de_DE.UTF-8",
    "fr_FR.UTF-8",
    "es_ES.UTF-8",
    "pt_BR.UTF-8",
    "Custom...",
};
const int NUM_LOCALE_PRESETS = (int)(sizeof (LOCALE_PRESETS) / sizeof (LOCALE_PRESETS[0]));

/* ─── Proton detection ──────────────────────────────────────── */

/* Scan a directory for subdirectories that look like Proton versions */
static void
scan_proton_dir (const char *dir, AppState *s)
{
    DIR *d = opendir (dir);
    if (d == NULL)
        return;

    struct dirent *entry;
    while ((entry = readdir (d)) != NULL && s->proton_count < MAX_PROTON - 1) {
        if (entry->d_name[0] == '.')
            continue;

        /* Check it has a proton executable inside */
        char candidate[MAX_PATH];
        snprintf (candidate, sizeof (candidate), "%s/%s", dir, entry->d_name);

        /* Resolve aliases such as ~/.steam/steam and ~/.steam/root so the
         * same Proton installation is only added once. */
        char canonical[PATH_MAX];
        if (realpath (candidate, canonical) == NULL)
            continue;

        char probe[MAX_PATH];
        int probe_len = snprintf (probe, sizeof (probe), "%s/proton", canonical);
        if (probe_len < 0 || (size_t)probe_len >= sizeof (probe))
            continue;
        if (access (probe, F_OK) == 0) {
            if (strlen (canonical) >= MAX_PATH)
                continue;
            int duplicate = 0;
            for (int i = 0; i < s->proton_count; i++) {
                if (strcmp (s->proton_list[i], canonical) == 0) {
                    duplicate = 1;
                    break;
                }
            }
            if (duplicate)
                continue;

            snprintf (s->proton_list[s->proton_count], MAX_PATH, "%s", canonical);
            snprintf (s->proton_labels[s->proton_count], MAX_NAME, "%s", entry->d_name);
            s->proton_count++;
        }
    }
    closedir (d);
}

void
detect_proton (AppState *s)
{
    s->proton_count = 0;
    const char *home = get_home ();
    char path[MAX_PATH];

    /* Steam Proton versions */
    snprintf (path, sizeof (path), "%s/.steam/steam/steamapps/common", home);
    scan_proton_dir (path, s);

    /* Also try .steam/root */
    snprintf (path, sizeof (path), "%s/.steam/root/steamapps/common", home);
    scan_proton_dir (path, s);

    /* GE-Proton / custom tools */
    snprintf (path, sizeof (path), "%s/.steam/root/compatibilitytools.d", home);
    scan_proton_dir (path, s);

    snprintf (path, sizeof (path), "%s/.steam/steam/compatibilitytools.d", home);
    scan_proton_dir (path, s);

    /* Add "Custom..." as last entry */
    snprintf (s->proton_labels[s->proton_count], MAX_NAME, "Custom...");
    s->proton_list[s->proton_count][0] = '\0';
    s->proton_count++;
}

/* ─── Icon install ──────────────────────────────────────────── */

/* Save one PNG in the user's icons directory and return its path. */
static int
install_icon (const char *src_path,
              const char *icon_name,
              char       *installed_path,
              size_t      path_size)
{
    int w, h, channels;
    unsigned char *src = stbi_load (src_path, &w, &h, &channels, 4);
    if (src == NULL) {
        fprintf (stderr, "Failed to load icon: %s\n", src_path);
        return 0;
    }

    char dir[MAX_PATH * 2];
    snprintf (dir, sizeof (dir), "%s/.local/share/icons", get_home ());
    mkdirp (dir);

    int path_len = snprintf (installed_path, path_size, "%s/%s.png", dir, icon_name);
    if (path_len < 0 || (size_t)path_len >= path_size ||
        !stbi_write_png (installed_path, w, h, 4, src, w * 4)) {
        fprintf (stderr, "Failed to write icon: %s\n", installed_path);
        stbi_image_free (src);
        return 0;
    }

    stbi_image_free (src);
    return 1;
}

/* ─── String helpers ────────────────────────────────────────── */

/* Sanitize a string for use as a filename / icon name */
static void
sanitize_name (const char *in, char *out, size_t outsz)
{
    size_t j = 0;
    for (size_t i = 0; in[i] && j < outsz - 1; i++) {
        char c = in[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_')
            out[j++] = c;
        else if (c == ' ')
            out[j++] = '-';
    }
    out[j] = '\0';
}

/* Escape special XDG characters for the Exec string.
 * Also doubles '%' which has special meaning in .desktop Exec= lines. */
static void
escape_desktop_string (const char *in, char *out, size_t outsz)
{
    size_t j = 0;
    for (size_t i = 0; in[i] && j < outsz - 2; i++) {
        char c = in[i];
        if (c == '"' || c == '\\' || c == '$' || c == '`')
            out[j++] = '\\';
        else if (c == '%' && j < outsz - 2)
            out[j++] = '%'; /* double: %% */
        out[j++] = c;
    }
    out[j] = '\0';
}

/* ─── Exec line ─────────────────────────────────────────────── */

/* Build the Exec= line */
static void
build_exec (const AppState *s, char *buf, size_t bufsz)
{
    const char *proton_path = "";
    if (s->proton_index < s->proton_count - 1)
        proton_path = s->proton_list[s->proton_index];
    else
        proton_path = s->custom_proton;

    /* Determine locale string */
    const char *locale = NULL;
    if (s->locale_index > 0 && s->locale_index < NUM_LOCALE_PRESETS - 1)
        locale = LOCALE_PRESETS[s->locale_index];
    else if (s->locale_index == NUM_LOCALE_PRESETS - 1)
        locale = s->custom_locale;

    /*
     * Correct umu-run order:
     * WINEPREFIX=... GAMEID=... STORE=... PROTONPATH=... [extras] umu-run "exe"
     */
    GString *cmd = g_string_new ("env ");

    /* WINEPREFIX */
    if (strlen (s->wineprefix) > 0)
        g_string_append_printf (cmd, "WINEPREFIX=\"%s\" ", s->wineprefix);

    /* GAMEID - always present */
    g_string_append (cmd, "GAMEID=ulwgl-0 ");

    /* PROTONPATH - quoted for spaces */
    g_string_append_printf (cmd, "PROTONPATH=\"%s\" ", proton_path);

    /* WoW64 */
    if (s->wow64)
        g_string_append (cmd, WOW64_FLAG " ");

    /* Wayland */
    if (s->wayland)
        g_string_append (cmd, WAYLAND_FLAG " ");

    /* Locale */
    if (locale != NULL && strlen (locale) > 0)
        g_string_append_printf (cmd, "LANG=%s LC_ALL=%s ", locale, locale);

    /* umu-run "exe" - quoted and escaped for spaces/special chars */
    {
        char escaped_exe[MAX_PATH * 2];
        escape_desktop_string (s->exe_path, escaped_exe, sizeof (escaped_exe));

        g_string_append_printf (cmd, "umu-run \"%s\"", escaped_exe);
    }

    g_strlcpy (buf, cmd->str, bufsz);
    g_string_free (cmd, TRUE);
}

/* ─── Shortcut creation ─────────────────────────────────────── */

/* Write the .desktop file and install icon (Wine/umu tab) */
void
create_shortcut (AppState *s)
{
    if (strlen (s->name) == 0) {
        snprintf (s->status_msg, sizeof (s->status_msg), "Error: Game name is empty.");
        s->status_ok = 0;
        return;
    }
    if (strlen (s->exe_path) == 0) {
        snprintf (s->status_msg, sizeof (s->status_msg), "Error: .exe path is empty.");
        s->status_ok = 0;
        return;
    }

    /* Proton path check */
    const char *proton_path = (s->proton_index < s->proton_count - 1)
        ? s->proton_list[s->proton_index]
        : s->custom_proton;
    if (strlen (proton_path) == 0) {
        snprintf (s->status_msg, sizeof (s->status_msg), "Error: Proton path is empty.");
        s->status_ok = 0;
        return;
    }

    char safe_name[MAX_NAME];
    sanitize_name (s->name, safe_name, sizeof (safe_name));
    if (strlen (safe_name) == 0)
        strcpy (safe_name, "game");

    const char *home = get_home ();

    /* Install icon if provided */
    char installed_icon[MAX_PATH * 2 + MAX_NAME + 8];
    int has_icon = (strlen (s->icon_path) > 0);
    if (has_icon && !install_icon (s->icon_path, safe_name,
                                   installed_icon, sizeof (installed_icon))) {
        has_icon = 0;
    }

    /* Build Exec line */
    char exec_line[MAX_PATH * 3];
    build_exec (s, exec_line, sizeof (exec_line));

    /* Write .desktop */
    char desktop_path[MAX_PATH];
    snprintf (desktop_path, sizeof (desktop_path),
              "%s/.local/share/applications/%s.desktop", home, safe_name);

    FILE *f = fopen (desktop_path, "w");
    if (f == NULL) {
        snprintf (s->status_msg, sizeof (s->status_msg),
                  "Error: cannot write %s: %s", desktop_path, strerror (errno));
        s->status_ok = 0;
        return;
    }

    fprintf (f, "[Desktop Entry]\n");
    fprintf (f, "Name=%s\n", s->name);
    fprintf (f, "Exec=%s\n", exec_line);
    fprintf (f, "Type=Application\n");
    {
        const char *cat_v = "Game";
        if (s->category_index > 0 && s->category_index < NUM_CATEGORIES - 1)
            cat_v = CATEGORIES[s->category_index].value;
        else if (s->category_index == NUM_CATEGORIES - 1 && strlen (s->custom_category) > 0)
            cat_v = s->custom_category;
        fprintf (f, "Categories=%s;\n", cat_v);
    }
    fprintf (f, "StartupNotify=false\n");
    if (has_icon)
        fprintf (f, "Icon=%s\n", installed_icon);
    fclose (f);

    snprintf (s->status_msg, sizeof (s->status_msg),
              "Done! \"%s\" added to menu.", s->name);
    s->status_ok = 1;
}

/* Create .desktop for a native binary or AppImage */
void
create_native_shortcut (NativeState *n)
{
    if (strlen (n->name) == 0) {
        snprintf (n->status_msg, sizeof (n->status_msg), "Error: App name is empty.");
        n->status_ok = 0;
        return;
    }
    if (strlen (n->bin_path) == 0) {
        snprintf (n->status_msg, sizeof (n->status_msg), "Error: Binary path is empty.");
        n->status_ok = 0;
        return;
    }

    /* Make sure binary is executable */
    chmod (n->bin_path, 0755);

    char safe_name[MAX_NAME];
    sanitize_name (n->name, safe_name, sizeof (safe_name));
    if (strlen (safe_name) == 0)
        strcpy (safe_name, "app");

    const char *home = get_home ();

    /* Install icon if provided */
    char installed_icon[MAX_PATH * 2 + MAX_NAME + 8];
    int has_icon = (strlen (n->icon_path) > 0);
    if (has_icon && !install_icon (n->icon_path, safe_name,
                                   installed_icon, sizeof (installed_icon))) {
        has_icon = 0;
    }

    /* Determine category value */
    const char *cat_value = "X-Other";
    if (n->category_index < NUM_CATEGORIES - 1)
        cat_value = CATEGORIES[n->category_index].value;
    else if (strlen (n->custom_category) > 0)
        cat_value = n->custom_category;

    /* Write .desktop */
    char desktop_path[MAX_PATH * 2];
    snprintf (desktop_path, sizeof (desktop_path),
              "%s/.local/share/applications/%s.desktop", home, safe_name);

    FILE *f = fopen (desktop_path, "w");
    if (f == NULL) {
        /* desktop_path can be ~MAX_PATH*2 chars; snprintf safely truncates */
        char errmsg[sizeof (n->status_msg)];
        snprintf (errmsg, sizeof (errmsg),
                  "Error: cannot write %.900s: %s", desktop_path, strerror (errno));
        snprintf (n->status_msg, sizeof (n->status_msg), "%s", errmsg);
        n->status_ok = 0;
        return;
    }

    char escaped_bin[MAX_PATH * 2];
    escape_desktop_string (n->bin_path, escaped_bin, sizeof (escaped_bin));

    fprintf (f, "[Desktop Entry]\n");
    fprintf (f, "Name=%s\n", n->name);
    fprintf (f, "Exec=\"%s\"\n", escaped_bin);
    fprintf (f, "Type=Application\n");
    fprintf (f, "Categories=%s;\n", cat_value);
    fprintf (f, "StartupNotify=false\n");
    if (has_icon)
        fprintf (f, "Icon=%s\n", installed_icon);
    fclose (f);

    snprintf (n->status_msg, sizeof (n->status_msg),
              "Done! \"%s\" added to menu.", n->name);
    n->status_ok = 1;
}
