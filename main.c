/*
 * asc - A Shortcut Creator
 * Requires: SDL2, Nuklear (nuklear.h), stb_image, stb_image_resize2, stb_image_write
 *
 * Build: see Makefile
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <fcntl.h>
#include <pthread.h>

/* SDL2 + OpenGL for Nuklear renderer */
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>

/* stb image libraries - implementation defined here */
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize2.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

/* Nuklear - immediate mode GUI */
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR
#define NK_INCLUDE_VERTEX_BUFFER_OUTPUT
#define NK_INCLUDE_FONT_BAKING
#define NK_INCLUDE_DEFAULT_FONT
#define NK_IMPLEMENTATION
#define NK_SDL_GL2_IMPLEMENTATION
#include "nuklear.h"
#include "nuklear_sdl_gl2.h"

/* ─── Constants ─────────────────────────────────────────────── */

#define WINDOW_WIDTH    680
#define WINDOW_HEIGHT   860
#define MAX_PATH        1024
#define MAX_NAME        256
#define MAX_PROTON      32
#define MAX_LOCALE      64
#define MAX_ENV_VARS    16

/* WoW64 environment variable used by umu/Proton */
#define WOW64_FLAG     "PROTON_USE_WINE64=1"
/* Wayland support flag */
#define WAYLAND_FLAG   "PROTON_ENABLE_WAYLAND=1"

/* Icon sizes to install into hicolor theme */
static const int ICON_SIZES[] = { 16, 32, 48, 64, 128, 256 };
#define NUM_ICON_SIZES  (int)(sizeof(ICON_SIZES) / sizeof(ICON_SIZES[0]))

/* Known locale presets */
static const char *LOCALE_PRESETS[] = {
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
#define NUM_LOCALE_PRESETS (int)(sizeof(LOCALE_PRESETS) / sizeof(LOCALE_PRESETS[0]))

/* XFCE menu categories — display name + .desktop value */
typedef struct { const char *label; const char *value; } Category;
static const Category CATEGORIES[] = {
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
#define NUM_CATEGORIES (int)(sizeof(CATEGORIES) / sizeof(CATEGORIES[0]))

/* Labels array for Nuklear combo (built at startup) */
static const char *CATEGORY_LABELS[NUM_CATEGORIES];

/* Native / AppImage state */
typedef struct {
    char name[MAX_NAME];
    char bin_path[MAX_PATH];
    char icon_path[MAX_PATH];
    int  category_index;
    char custom_category[MAX_NAME];

    char status_msg[2048];
    int  status_ok;
    int  show_status;
} NativeState;

/* Active tab: 0 = umu/Wine, 1 = Native/AppImage */
static int g_active_tab = 0;


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
    int  show_status;
} AppState;

/* ─── Helpers ────────────────────────────────────────────────── */

/* Safe command runner via fork/execv — avoids shell injection from system().
 * argv must be NULL-terminated. */
static int run_cmd(const char *path, char *const argv[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execv(path, argv);
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static const char *get_home(void) {
    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    return home ? home : "/tmp";
}

/* Create directory and parents (like mkdir -p) */
static int mkdirp(const char *path) {
    char tmp[MAX_PATH * 2];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);
    if (tmp[len - 1] == '/') tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755);
}

/* Scan a directory for subdirectories that look like Proton versions */
static void scan_proton_dir(const char *dir, AppState *s) {
    DIR *d = opendir(dir);
    if (!d) return;

    struct dirent *entry;
    while ((entry = readdir(d)) != NULL && s->proton_count < MAX_PROTON - 1) {
        if (entry->d_name[0] == '.') continue;

        /* Check it has a proton executable inside */
        char probe[MAX_PATH];
        snprintf(probe, sizeof(probe), "%s/%s/proton", dir, entry->d_name);
        if (access(probe, F_OK) == 0) {
            snprintf(s->proton_list[s->proton_count], MAX_PATH, "%s/%s", dir, entry->d_name);
            snprintf(s->proton_labels[s->proton_count], MAX_NAME, "%s", entry->d_name);
            s->proton_count++;
        }
    }
    closedir(d);
}

static void detect_proton(AppState *s) {
    s->proton_count = 0;
    const char *home = get_home();
    char path[MAX_PATH];

    /* Steam Proton versions */
    snprintf(path, sizeof(path), "%s/.steam/steam/steamapps/common", home);
    scan_proton_dir(path, s);

    /* Also try .steam/root */
    snprintf(path, sizeof(path), "%s/.steam/root/steamapps/common", home);
    scan_proton_dir(path, s);

    /* GE-Proton / custom tools */
    snprintf(path, sizeof(path), "%s/.steam/root/compatibilitytools.d", home);
    scan_proton_dir(path, s);

    snprintf(path, sizeof(path), "%s/.steam/steam/compatibilitytools.d", home);
    scan_proton_dir(path, s);

    /* Add "Custom..." as last entry */
    snprintf(s->proton_labels[s->proton_count], MAX_NAME, "Custom...");
    s->proton_list[s->proton_count][0] = '\0';
    s->proton_count++;
}

/* Install icon into hicolor theme at all required sizes */
static int install_icon(const char *src_path, const char *icon_name) {
    int w, h, channels;
    unsigned char *src = stbi_load(src_path, &w, &h, &channels, 4);
    if (!src) {
        fprintf(stderr, "Failed to load icon: %s\n", src_path);
        return 0;
    }

    const char *home = get_home();
    int all_ok = 1;

    for (int i = 0; i < NUM_ICON_SIZES; i++) {
        int sz = ICON_SIZES[i];
        unsigned char *dst = malloc(sz * sz * 4);
        if (!dst) { all_ok = 0; continue; }

        stbir_resize_uint8_linear(src, w, h, 0, dst, sz, sz, 0, STBIR_RGBA);

        char dir[MAX_PATH * 2], file[MAX_PATH * 2];
        snprintf(dir, sizeof(dir),
                 "%s/.local/share/icons/hicolor/%dx%d/apps", home, sz, sz);
        mkdirp(dir);
        snprintf(file, sizeof(file), "%s/%s.png", dir, icon_name);

        if (!stbi_write_png(file, sz, sz, 4, dst, sz * 4)) {
            fprintf(stderr, "Failed to write icon: %s\n", file);
            all_ok = 0;
        }
        free(dst);
    }

    stbi_image_free(src);
    return all_ok;
}

/* Sanitize a string for use as a filename / icon name */
static void sanitize_name(const char *in, char *out, size_t outsz) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j < outsz - 1; i++) {
        char c = in[i];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_') {
            out[j++] = c;
        } else if (c == ' ') {
            out[j++] = '-';
        }
    }
    out[j] = '\0';
}

/* Escape special XDG characters for the Exec string.
 * Also doubles '%' which has special meaning in .desktop Exec= lines. */
static void escape_desktop_string(const char *in, char *out, size_t outsz) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j < outsz - 2; i++) {
        char c = in[i];
        if (c == '"' || c == '\\' || c == '$' || c == '`') {
            out[j++] = '\\';
        } else if (c == '%' && j < outsz - 2) {
            out[j++] = '%'; /* double: %% */
        }
        out[j++] = c;
    }
    out[j] = '\0';
}

/* Build the Exec= line */
static void build_exec(const AppState *s, char *buf, size_t bufsz) {
    const char *proton_path = "";
    if (s->proton_index < s->proton_count - 1) {
        proton_path = s->proton_list[s->proton_index];
    } else {
        proton_path = s->custom_proton;
    }

    /* Determine locale string */
    const char *locale = NULL;
    if (s->locale_index > 0 && s->locale_index < NUM_LOCALE_PRESETS - 1) {
        locale = LOCALE_PRESETS[s->locale_index];
    } else if (s->locale_index == NUM_LOCALE_PRESETS - 1) {
        locale = s->custom_locale;
    }

    /*
     * Correct umu-run order:
     * WINEPREFIX=... GAMEID=... STORE=... PROTONPATH=... [extras] umu-run "exe"
     */
    char *buf2 = malloc(bufsz);
    if (!buf2) { buf[0] = '\0'; return; }
    snprintf(buf2, bufsz, "env ");

    /* WINEPREFIX */
    if (strlen(s->wineprefix) > 0) {
        char tmp[MAX_PATH + 20];
        snprintf(tmp, sizeof(tmp), "WINEPREFIX=\"%s\" ", s->wineprefix);
        strncat(buf2, tmp, bufsz - strlen(buf2) - 1);
    }

    /* GAMEID - always present */
    strncat(buf2, "GAMEID=ulwgl-0 ", bufsz - strlen(buf2) - 1);

    /* PROTONPATH - quoted for spaces */
    {
        char tmp[MAX_PATH + 20];
        snprintf(tmp, sizeof(tmp), "PROTONPATH=\"%s\" ", proton_path);
        strncat(buf2, tmp, bufsz - strlen(buf2) - 1);
    }

    /* WoW64 */
    if (s->wow64) {
        strncat(buf2, WOW64_FLAG " ", bufsz - strlen(buf2) - 1);
    }

    /* Wayland */
    if (s->wayland) {
        strncat(buf2, WAYLAND_FLAG " ", bufsz - strlen(buf2) - 1);
    }

    /* Locale */
    if (locale && strlen(locale) > 0) {
        char tmp[256];
        snprintf(tmp, sizeof(tmp), "LANG=%s LC_ALL=%s ", locale, locale);
        strncat(buf2, tmp, bufsz - strlen(buf2) - 1);
    }

    /* umu-run "exe" - quoted and escaped for spaces/special chars */
    {
        char escaped_exe[MAX_PATH * 2];
        escape_desktop_string(s->exe_path, escaped_exe, sizeof(escaped_exe));
        
        char tmp[MAX_PATH * 2 + 12];
        snprintf(tmp, sizeof(tmp), "umu-run \"%s\"", escaped_exe);
        strncat(buf2, tmp, bufsz - strlen(buf2) - 1);
    }

    snprintf(buf, bufsz, "%s", buf2);
    free(buf2);
}

/* Write the .desktop file and install icon */
static void create_shortcut(AppState *s) {
    if (strlen(s->name) == 0) {
        snprintf(s->status_msg, sizeof(s->status_msg), "Error: Game name is empty.");
        s->status_ok = 0; s->show_status = 1; return;
    }
    if (strlen(s->exe_path) == 0) {
        snprintf(s->status_msg, sizeof(s->status_msg), "Error: .exe path is empty.");
        s->status_ok = 0; s->show_status = 1; return;
    }

    /* Proton path check */
    const char *proton_path = (s->proton_index < s->proton_count - 1)
        ? s->proton_list[s->proton_index]
        : s->custom_proton;
    if (strlen(proton_path) == 0) {
        snprintf(s->status_msg, sizeof(s->status_msg), "Error: Proton path is empty.");
        s->status_ok = 0; s->show_status = 1; return;
    }

    char safe_name[MAX_NAME];
    sanitize_name(s->name, safe_name, sizeof(safe_name));
    if (strlen(safe_name) == 0) strcpy(safe_name, "game");

    const char *home = get_home();

    /* Install icon if provided */
    int has_icon = (strlen(s->icon_path) > 0);
    if (has_icon && !install_icon(s->icon_path, safe_name))
        has_icon = 0;

    /* Build Exec line */
    char exec_line[MAX_PATH * 3];
    build_exec(s, exec_line, sizeof(exec_line));

    /* Write .desktop */
    char desktop_path[MAX_PATH];
    snprintf(desktop_path, sizeof(desktop_path),
             "%s/.local/share/applications/%s.desktop", home, safe_name);

    FILE *f = fopen(desktop_path, "w");
    if (!f) {
        snprintf(s->status_msg, sizeof(s->status_msg),
                 "Error: cannot write %s: %s", desktop_path, strerror(errno));
        s->status_ok = 0; s->show_status = 1; return;
    }

    fprintf(f, "[Desktop Entry]\n");
    fprintf(f, "Name=%s\n", s->name);
    fprintf(f, "Exec=%s\n", exec_line);
    fprintf(f, "Type=Application\n");
    {
        const char *cat_v = "Game";
        if (s->category_index > 0 && s->category_index < NUM_CATEGORIES - 1)
            cat_v = CATEGORIES[s->category_index].value;
        else if (s->category_index == NUM_CATEGORIES - 1 && strlen(s->custom_category) > 0)
            cat_v = s->custom_category;
        fprintf(f, "Categories=%s;\n", cat_v);
    }
    fprintf(f, "StartupNotify=false\n");
    if (has_icon) fprintf(f, "Icon=%s\n", safe_name);
    fclose(f);

    /* Refresh desktop database */
    char apps_dir[MAX_PATH];
    snprintf(apps_dir, sizeof(apps_dir), "%s/.local/share/applications", home);
    { char *a[] = { "/usr/bin/update-desktop-database", apps_dir, NULL };
      run_cmd("/usr/bin/update-desktop-database", a); }

    /* Refresh icon cache */
    { char icons_dir[MAX_PATH];
      snprintf(icons_dir, sizeof(icons_dir), "%s/.local/share/icons/hicolor", home);
      char *a[] = { "/usr/bin/gtk-update-icon-cache", "-f", "-t", icons_dir, NULL };
      run_cmd("/usr/bin/gtk-update-icon-cache", a); }

    snprintf(s->status_msg, sizeof(s->status_msg),
             "Done! \"%s\" added to Games menu.", s->name);
    s->status_ok = 1;
    s->show_status = 1;
}

/* ─── Async file picker (runs yad in background thread) ──── */

typedef struct {
    char cmd[1024];        /* yad command to run */
    char result[MAX_PATH]; /* filled on completion */
    int  done;             /* 0 = running, 1 = done */
    int  ok;               /* 1 = user picked something */
    pthread_mutex_t mutex;
} PickerState;

static PickerState g_picker = { .done = 1, .ok = 0 };

static void *picker_thread(void *arg) {
    PickerState *ps = (PickerState *)arg;

    FILE *p = popen(ps->cmd, "r");
    if (p) {
        char buf[MAX_PATH] = {0};
        if (fgets(buf, sizeof(buf), p)) {
            size_t len = strlen(buf);
            if (len > 0 && buf[len-1] == '\n') buf[len-1] = '\0';
            pthread_mutex_lock(&ps->mutex);
            snprintf(ps->result, sizeof(ps->result), "%s", buf);
            ps->ok = (strlen(buf) > 0);
            pthread_mutex_unlock(&ps->mutex);
        }
        pclose(p);
    }

    pthread_mutex_lock(&ps->mutex);
    ps->done = 1;
    pthread_mutex_unlock(&ps->mutex);
    return NULL;
}

/* Launch yad asynchronously. Returns 1 if launched, 0 if already busy. */
static int launch_picker(const char *cmd) {
    pthread_mutex_lock(&g_picker.mutex);
    if (!g_picker.done) {
        pthread_mutex_unlock(&g_picker.mutex);
        return 0; /* already open */
    }
    snprintf(g_picker.cmd, sizeof(g_picker.cmd), "%s", cmd);
    g_picker.done = 0;
    g_picker.ok   = 0;
    g_picker.result[0] = '\0';
    pthread_mutex_unlock(&g_picker.mutex);

    pthread_t tid;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    int ret = pthread_create(&tid, &attr, picker_thread, &g_picker);
    pthread_attr_destroy(&attr);
    
    if (ret != 0) {
        /* Failed to spawn thread, release the picker lock */
        pthread_mutex_lock(&g_picker.mutex);
        g_picker.done = 1;
        pthread_mutex_unlock(&g_picker.mutex);
        return 0;
    }
    return 1;
}

/* Call each frame to check if picker finished. Returns 1 and fills out if done. */
static int picker_check(char *out, size_t outsz) {
    pthread_mutex_lock(&g_picker.mutex);
    if (!g_picker.done) {
        pthread_mutex_unlock(&g_picker.mutex);
        return 0;
    }
    int ok = g_picker.ok;
    if (ok) snprintf(out, outsz, "%s", g_picker.result);
    pthread_mutex_unlock(&g_picker.mutex);
    return ok ? 2 : 1; /* 2 = picked, 1 = cancelled/done */
}

/* Which field is waiting for a picker result */
typedef enum {
    PICK_NONE = 0,
    PICK_EXE,
    PICK_ICON,
    PICK_PROTON_DIR,
    PICK_WINEPREFIX,
    PICK_NATIVE_BIN,
    PICK_NATIVE_ICON,
} PickTarget;

static PickTarget g_pick_target = PICK_NONE;

static void start_file_pick(PickTarget target, const char *title,
                             const char *filter_name, const char *filter_pattern) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd),
             "yad --file-selection --title=\"%s\" "
             "--file-filter=\"%s | %s\" 2>/dev/null",
             title, filter_name, filter_pattern);
    if (launch_picker(cmd))
        g_pick_target = target;
}

static void start_dir_pick(PickTarget target, const char *title) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "yad --file-selection --directory --title=\"%s\" 2>/dev/null", title);
    if (launch_picker(cmd))
        g_pick_target = target;
}

/* Create .desktop for a native binary or AppImage */
static void create_native_shortcut(NativeState *n) {
    if (strlen(n->name) == 0) {
        snprintf(n->status_msg, sizeof(n->status_msg), "Error: App name is empty.");
        n->status_ok = 0; n->show_status = 1; return;
    }
    if (strlen(n->bin_path) == 0) {
        snprintf(n->status_msg, sizeof(n->status_msg), "Error: Binary path is empty.");
        n->status_ok = 0; n->show_status = 1; return;
    }

    /* Make sure binary is executable */
    chmod(n->bin_path, 0755);

    char safe_name[MAX_NAME];
    sanitize_name(n->name, safe_name, sizeof(safe_name));
    if (strlen(safe_name) == 0) strcpy(safe_name, "app");

    const char *home = get_home();

    /* Install icon if provided */
    int has_icon = (strlen(n->icon_path) > 0);
    if (has_icon && !install_icon(n->icon_path, safe_name))
        has_icon = 0;

    /* Determine category value */
    const char *cat_value = "X-Other";
    if (n->category_index < NUM_CATEGORIES - 1) {
        cat_value = CATEGORIES[n->category_index].value;
    } else {
        /* Custom */
        if (strlen(n->custom_category) > 0)
            cat_value = n->custom_category;
    }

    /* Write .desktop */
    char desktop_path[MAX_PATH * 2];
    snprintf(desktop_path, sizeof(desktop_path),
             "%s/.local/share/applications/%s.desktop", home, safe_name);

    FILE *f = fopen(desktop_path, "w");
    if (!f) {
        snprintf(n->status_msg, sizeof(n->status_msg),
                 "Error: cannot write %s: %s", desktop_path, strerror(errno));
        n->status_ok = 0; n->show_status = 1; return;
    }

    char escaped_bin[MAX_PATH * 2];
    escape_desktop_string(n->bin_path, escaped_bin, sizeof(escaped_bin));

    fprintf(f, "[Desktop Entry]\n");
    fprintf(f, "Name=%s\n", n->name);
    fprintf(f, "Exec=\"%s\"\n", escaped_bin);
    fprintf(f, "Type=Application\n");
    fprintf(f, "Categories=%s;\n", cat_value);
    fprintf(f, "StartupNotify=false\n");
    if (has_icon) fprintf(f, "Icon=%s\n", safe_name);
    fclose(f);

    /* Refresh desktop db and icon cache */
    char apps_dir[MAX_PATH];
    snprintf(apps_dir, sizeof(apps_dir), "%s/.local/share/applications", home);
    { char *a[] = { "/usr/bin/update-desktop-database", apps_dir, NULL };
      run_cmd("/usr/bin/update-desktop-database", a); }
    { char icons_dir[MAX_PATH];
      snprintf(icons_dir, sizeof(icons_dir), "%s/.local/share/icons/hicolor", home);
      char *a[] = { "/usr/bin/gtk-update-icon-cache", "-f", "-t", icons_dir, NULL };
      run_cmd("/usr/bin/gtk-update-icon-cache", a); }

    snprintf(n->status_msg, sizeof(n->status_msg),
             "Done! \"%s\" added to menu.", n->name);
    n->status_ok = 1; n->show_status = 1;
}

/* ─── GUI ────────────────────────────────────────────────────── */

static void draw_ui(struct nk_context *ctx, AppState *s, NativeState *n,
                    struct nk_font *font_ui, struct nk_font *font_mono) {
#define USE_MONO nk_style_set_font(ctx, &font_mono->handle)
#define USE_UI   nk_style_set_font(ctx, &font_ui->handle)
    int is_custom_proton = (s->proton_index == s->proton_count - 1);
    int is_custom_locale = (s->locale_index == NUM_LOCALE_PRESETS - 1);
    int is_custom_cat    = (n->category_index == NUM_CATEGORIES - 1);

    int is_custom_wine_cat = (s->category_index == NUM_CATEGORIES - 1);
    int win_h = WINDOW_HEIGHT;
    if (g_active_tab == 0) {
        if (is_custom_proton)   win_h += 40;
        if (is_custom_locale)   win_h += 40;
        if (is_custom_wine_cat) win_h += 58;
    } else {
        if (is_custom_cat) win_h += 58;
    }

    if (nk_begin(ctx, "asc - A Shortcut Creator",
                 nk_rect(0, 0, WINDOW_WIDTH, win_h),
                 NK_WINDOW_NO_SCROLLBAR)) {

        /* ── Tabs ── */
        nk_layout_row_begin(ctx, NK_STATIC, 32, 2);
        nk_layout_row_push(ctx, (WINDOW_WIDTH - 32) / 2);
        if (g_active_tab == 0) {
            nk_style_push_style_item(ctx, &ctx->style.button.normal,
                                     nk_style_item_color(nk_rgb(0xc8, 0xa9, 0x78)));
            nk_style_push_color(ctx, &ctx->style.button.text_normal, nk_rgb(0x1c, 0x1a, 0x1a));
        } else {
            nk_style_push_style_item(ctx, &ctx->style.button.normal,
                                     nk_style_item_color(nk_rgb(0x30, 0x2a, 0x26)));
            nk_style_push_color(ctx, &ctx->style.button.text_normal, nk_rgb(0x9a, 0x8c, 0x80));
        }
        if (nk_button_label(ctx, "Wine / umu")) g_active_tab = 0;
        nk_style_pop_color(ctx);
        nk_style_pop_style_item(ctx);

        nk_layout_row_push(ctx, (WINDOW_WIDTH - 32) / 2);
        if (g_active_tab == 1) {
            nk_style_push_style_item(ctx, &ctx->style.button.normal,
                                     nk_style_item_color(nk_rgb(0xc8, 0xa9, 0x78)));
            nk_style_push_color(ctx, &ctx->style.button.text_normal, nk_rgb(0x1c, 0x1a, 0x1a));
        } else {
            nk_style_push_style_item(ctx, &ctx->style.button.normal,
                                     nk_style_item_color(nk_rgb(0x30, 0x2a, 0x26)));
            nk_style_push_color(ctx, &ctx->style.button.text_normal, nk_rgb(0x9a, 0x8c, 0x80));
        }
        if (nk_button_label(ctx, "Native / AppImage")) g_active_tab = 1;
        nk_style_pop_color(ctx);
        nk_style_pop_style_item(ctx);
        nk_layout_row_end(ctx);
        nk_layout_row_dynamic(ctx, 8, 1);

        /* ════════════════════════════════════════════════
         * TAB 0 — Wine / umu
         * ════════════════════════════════════════════════ */
        if (g_active_tab == 0) {

            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label_colored(ctx, "Game Name", NK_TEXT_LEFT, nk_rgb(0x9a, 0x8c, 0x80));
            nk_layout_row_dynamic(ctx, 32, 1);
            USE_MONO;
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, s->name,
                                            sizeof(s->name), nk_filter_default);
            USE_UI;
            nk_layout_row_dynamic(ctx, 10, 1);

            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label_colored(ctx, "Category", NK_TEXT_LEFT, nk_rgb(0x9a, 0x8c, 0x80));
            nk_layout_row_dynamic(ctx, 32, 1);
            s->category_index = nk_combo(ctx, CATEGORY_LABELS, NUM_CATEGORIES,
                                          s->category_index, 28,
                                          nk_vec2(WINDOW_WIDTH - 30, 300));
            if (s->category_index == NUM_CATEGORIES - 1) {
                nk_layout_row_dynamic(ctx, 10, 1);
                nk_layout_row_dynamic(ctx, 18, 1);
                nk_label_colored(ctx, "Custom category name", NK_TEXT_LEFT, nk_rgb(0x9a, 0x8c, 0x80));
                nk_layout_row_dynamic(ctx, 32, 1);
                USE_MONO;
                nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, s->custom_category,
                                                sizeof(s->custom_category), nk_filter_default);
                USE_UI;
            }
            nk_layout_row_dynamic(ctx, 10, 1);

            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label_colored(ctx, "Game Executable (.exe)", NK_TEXT_LEFT, nk_rgb(0x9a, 0x8c, 0x80));
            nk_layout_row_begin(ctx, NK_STATIC, 32, 2);
            nk_layout_row_push(ctx, WINDOW_WIDTH - 156);
            USE_MONO;
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, s->exe_path,
                                            sizeof(s->exe_path), nk_filter_default);
            USE_UI;
            nk_layout_row_push(ctx, 100);
            if (nk_button_label(ctx, "Browse..."))
                start_file_pick(PICK_EXE, "Select Game Executable",
                                "Windows Executable", "*.exe *.EXE");
            nk_layout_row_end(ctx);
            nk_layout_row_dynamic(ctx, 10, 1);

            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label_colored(ctx, "Proton Version", NK_TEXT_LEFT, nk_rgb(0x9a, 0x8c, 0x80));
            nk_layout_row_dynamic(ctx, 32, 1);
            if (s->proton_count > 0) {
                const char *labels[MAX_PROTON];
                for (int i = 0; i < s->proton_count; i++)
                    labels[i] = s->proton_labels[i];
                s->proton_index = nk_combo(ctx, labels, s->proton_count,
                                            s->proton_index, 28,
                                            nk_vec2(WINDOW_WIDTH - 30, 200));
            } else {
                nk_label_colored(ctx, "No Proton versions detected.", NK_TEXT_LEFT, nk_rgb(0x9a, 0x8c, 0x80));
            }
            if (is_custom_proton) {
                nk_layout_row_begin(ctx, NK_STATIC, 32, 2);
                nk_layout_row_push(ctx, WINDOW_WIDTH - 156);
                USE_MONO;
                nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, s->custom_proton,
                                                sizeof(s->custom_proton), nk_filter_default);
                USE_UI;
                nk_layout_row_push(ctx, 100);
                if (nk_button_label(ctx, "Browse..."))
                    start_dir_pick(PICK_PROTON_DIR, "Select Proton Directory");
                nk_layout_row_end(ctx);
            }
            nk_layout_row_dynamic(ctx, 10, 1);

            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label_colored(ctx, "Locale (LANG / LC_ALL)", NK_TEXT_LEFT, nk_rgb(0x9a, 0x8c, 0x80));
            nk_layout_row_dynamic(ctx, 32, 1);
            s->locale_index = nk_combo(ctx, LOCALE_PRESETS, NUM_LOCALE_PRESETS,
                                        s->locale_index, 28,
                                        nk_vec2(WINDOW_WIDTH - 30, 200));
            if (is_custom_locale) {
                nk_layout_row_dynamic(ctx, 32, 1);
                USE_MONO;
                nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, s->custom_locale,
                                                sizeof(s->custom_locale), nk_filter_default);
                USE_UI;
            }
            nk_layout_row_dynamic(ctx, 10, 1);

            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label_colored(ctx, "Icon (PNG recommended)", NK_TEXT_LEFT, nk_rgb(0x9a, 0x8c, 0x80));
            nk_layout_row_begin(ctx, NK_STATIC, 32, 2);
            nk_layout_row_push(ctx, WINDOW_WIDTH - 156);
            USE_MONO;
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, s->icon_path,
                                            sizeof(s->icon_path), nk_filter_default);
            USE_UI;
            nk_layout_row_push(ctx, 100);
            if (nk_button_label(ctx, "Browse..."))
                start_file_pick(PICK_ICON, "Select Icon",
                                "Images", "*.png *.PNG *.jpg *.jpeg *.JPG");
            nk_layout_row_end(ctx);
            nk_layout_row_dynamic(ctx, 10, 1);

            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label_colored(ctx, "WINEPREFIX (optional)", NK_TEXT_LEFT, nk_rgb(0x9a, 0x8c, 0x80));
            nk_layout_row_begin(ctx, NK_STATIC, 32, 2);
            nk_layout_row_push(ctx, WINDOW_WIDTH - 156);
            USE_MONO;
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, s->wineprefix,
                                            sizeof(s->wineprefix), nk_filter_default);
            USE_UI;
            nk_layout_row_push(ctx, 100);
            if (nk_button_label(ctx, "Browse..."))
                start_dir_pick(PICK_WINEPREFIX, "Select WINEPREFIX Directory");
            nk_layout_row_end(ctx);
            nk_layout_row_dynamic(ctx, 10, 1);

            nk_layout_row_dynamic(ctx, 28, 1);
            nk_checkbox_label(ctx, "Enable WoW64 (PROTON_USE_WINE64=1)", &s->wow64);
            nk_layout_row_dynamic(ctx, 28, 1);
            nk_checkbox_label(ctx, "Enable Wayland (PROTON_ENABLE_WAYLAND=1)", &s->wayland);
            nk_layout_row_dynamic(ctx, 14, 1);
            nk_layout_row_dynamic(ctx, 1, 1);
            nk_rule_horizontal(ctx, ctx->style.window.border_color, nk_false);
            nk_layout_row_dynamic(ctx, 12, 1);

            nk_layout_row_dynamic(ctx, 42, 1);
            if (nk_button_label(ctx, "Create Shortcut"))
                create_shortcut(s);

            nk_layout_row_begin(ctx, NK_STATIC, 28, 1);
            nk_layout_row_push(ctx, 110);
            if (nk_button_label(ctx, "Shortcuts")) {
                char cmd[MAX_PATH + 32];
                snprintf(cmd, sizeof(cmd),
                         "xdg-open \"%s/.local/share/applications\" &", get_home());
                system(cmd);
            }
            nk_layout_row_end(ctx);

            if (s->show_status) {
                nk_layout_row_dynamic(ctx, 6, 1);
                nk_layout_row_dynamic(ctx, 22, 1);
                struct nk_color col = s->status_ok ? nk_rgb(80, 200, 100) : nk_rgb(220, 80, 80);
                nk_label_colored(ctx, s->status_msg, NK_TEXT_CENTERED, col);
            }

        /* ════════════════════════════════════════════════
         * TAB 1 — Native / AppImage
         * ════════════════════════════════════════════════ */
        } else {

            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label_colored(ctx, "App Name", NK_TEXT_LEFT, nk_rgb(0x9a, 0x8c, 0x80));
            nk_layout_row_dynamic(ctx, 32, 1);
            USE_MONO;
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, n->name,
                                            sizeof(n->name), nk_filter_default);
            USE_UI;
            nk_layout_row_dynamic(ctx, 10, 1);
            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label_colored(ctx, "Category", NK_TEXT_LEFT, nk_rgb(0x9a, 0x8c, 0x80));
            nk_layout_row_dynamic(ctx, 32, 1);
            n->category_index = nk_combo(ctx, CATEGORY_LABELS, NUM_CATEGORIES,
                                          n->category_index, 28,
                                          nk_vec2(WINDOW_WIDTH - 30, 300));
            if (is_custom_cat) {
                nk_layout_row_dynamic(ctx, 10, 1);
                nk_layout_row_dynamic(ctx, 18, 1);
                nk_label_colored(ctx, "Custom category name", NK_TEXT_LEFT, nk_rgb(0x9a, 0x8c, 0x80));
                nk_layout_row_dynamic(ctx, 32, 1);
                USE_MONO;
                nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, n->custom_category,
                                                sizeof(n->custom_category), nk_filter_default);
                USE_UI;
            }
            nk_layout_row_dynamic(ctx, 10, 1);


            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label_colored(ctx, "Binary / AppImage", NK_TEXT_LEFT, nk_rgb(0x9a, 0x8c, 0x80));
            nk_layout_row_begin(ctx, NK_STATIC, 32, 2);
            nk_layout_row_push(ctx, WINDOW_WIDTH - 156);
            USE_MONO;
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, n->bin_path,
                                            sizeof(n->bin_path), nk_filter_default);
            USE_UI;
            nk_layout_row_push(ctx, 100);
            if (nk_button_label(ctx, "Browse..."))
                start_file_pick(PICK_NATIVE_BIN, "Select Binary or AppImage",
                                "Executable", "*.AppImage *.appimage *");
            nk_layout_row_end(ctx);
            nk_layout_row_dynamic(ctx, 10, 1);

            nk_layout_row_dynamic(ctx, 18, 1);
            nk_label_colored(ctx, "Icon (PNG recommended)", NK_TEXT_LEFT, nk_rgb(0x9a, 0x8c, 0x80));
            nk_layout_row_begin(ctx, NK_STATIC, 32, 2);
            nk_layout_row_push(ctx, WINDOW_WIDTH - 156);
            USE_MONO;
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, n->icon_path,
                                            sizeof(n->icon_path), nk_filter_default);
            USE_UI;
            nk_layout_row_push(ctx, 100);
            if (nk_button_label(ctx, "Browse..."))
                start_file_pick(PICK_NATIVE_ICON, "Select Icon",
                                "Images", "*.png *.PNG *.jpg *.jpeg *.JPG");
            nk_layout_row_end(ctx);
            nk_layout_row_dynamic(ctx, 10, 1);

            nk_layout_row_dynamic(ctx, 14, 1);
            nk_layout_row_dynamic(ctx, 1, 1);
            nk_rule_horizontal(ctx, ctx->style.window.border_color, nk_false);
            nk_layout_row_dynamic(ctx, 12, 1);

            nk_layout_row_dynamic(ctx, 42, 1);
            if (nk_button_label(ctx, "Create Shortcut"))
                create_native_shortcut(n);

            nk_layout_row_begin(ctx, NK_STATIC, 28, 1);
            nk_layout_row_push(ctx, 110);
            if (nk_button_label(ctx, "Shortcuts")) {
                char cmd[MAX_PATH + 32];
                snprintf(cmd, sizeof(cmd),
                         "xdg-open \"%s/.local/share/applications\" &", get_home());
                system(cmd);
            }
            nk_layout_row_end(ctx);

            if (n->show_status) {
                nk_layout_row_dynamic(ctx, 6, 1);
                nk_layout_row_dynamic(ctx, 22, 1);
                struct nk_color col = n->status_ok ? nk_rgb(80, 200, 100) : nk_rgb(220, 80, 80);
                nk_label_colored(ctx, n->status_msg, NK_TEXT_CENTERED, col);
            }
        }

        nk_layout_row_dynamic(ctx, 8, 1);
    }
    nk_end(ctx);
}

/* ─── Main ───────────────────────────────────────────────────── */

int main(void) {
    /* Init state */
    AppState s;
    memset(&s, 0, sizeof(s));
    s.proton_index = 0;
    s.locale_index = 0;
    s.wow64 = 0;
    s.wayland = 0;
    s.show_status = 0;

    NativeState n;
    memset(&n, 0, sizeof(n));
    n.category_index = 0; /* Games by default */
    n.show_status = 0;

    /* Build category labels array for Nuklear */
    for (int i = 0; i < NUM_CATEGORIES; i++)
        CATEGORY_LABELS[i] = CATEGORIES[i].label;

    pthread_mutex_init(&g_picker.mutex, NULL);
    g_picker.done = 1;

    detect_proton(&s);

    /* SDL2 init */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    SDL_Window *win = SDL_CreateWindow(
        "asc - A Shortcut Creator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN);

    if (!win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glctx = SDL_GL_CreateContext(win);
    SDL_GL_SetSwapInterval(1);

    /* Nuklear init */
    struct nk_context *ctx = nk_sdl_init(win);

    /* Detect best font with CJK support.
     * Priority: known good Japanese fonts → fc-match :lang=ja → fc-match default */
    char font_path[MAX_PATH] = {0};
    {
        /* Preferred Japanese-capable fonts in order */
        static const char *preferred[] = {
            "BIZ UDGothic",
            "BIZ UDPGothic",
            "Noto Sans CJK JP",
            "Noto Sans JP",
            "IPAGothic",
            "IPA Gothic",
            "VL Gothic",
            "TakaoGothic",
            NULL
        };

        for (int i = 0; preferred[i] && font_path[0] == '\0'; i++) {
            char cmd[256];
            snprintf(cmd, sizeof(cmd),
                     "fc-match \"%s\" file | sed 's/.*file=//;s/:.*//'", preferred[i]);
            FILE *fc = popen(cmd, "r");
            if (fc) {
                char tmp[MAX_PATH] = {0};
                if (fgets(tmp, sizeof(tmp), fc)) {
                    size_t len = strlen(tmp);
                    if (len > 0 && tmp[len-1] == '\n') tmp[len-1] = '\0';
                    /* fc-match always returns something - verify it's actually the font we want */
                    char verify[256];
                    snprintf(verify, sizeof(verify),
                             "fc-match \"%s\" family | grep -i \"%s\" > /dev/null 2>&1",
                             preferred[i], preferred[i]);
                    if (system(verify) == 0)
                        snprintf(font_path, sizeof(font_path), "%s", tmp);
                }
                pclose(fc);
            }
        }

        /* Fallback: ask fontconfig for any font with Japanese support */
        if (font_path[0] == '\0') {
            FILE *fc = popen("fc-match :lang=ja file | sed 's/.*file=//;s/:.*//'", "r");
            if (fc) {
                if (fgets(font_path, sizeof(font_path), fc)) {
                    size_t len = strlen(font_path);
                    if (len > 0 && font_path[len-1] == '\n') font_path[len-1] = '\0';
                }
                pclose(fc);
            }
        }

        /* Last resort: just use system default */
        if (font_path[0] == '\0') {
            FILE *fc = popen("fc-match : file | sed 's/.*file=//;s/:.*//'", "r");
            if (fc) {
                if (fgets(font_path, sizeof(font_path), fc)) {
                    size_t len = strlen(font_path);
                    if (len > 0 && font_path[len-1] == '\n') font_path[len-1] = '\0';
                }
                pclose(fc);
            }
        }
    }

    /* Detect best monospace font with CJK support */
    char mono_path[MAX_PATH] = {0};
    {
        static const char *preferred_mono[] = {
            "M PLUS 1 Code",
            "M+ 1 Code",
            "Noto Sans Mono CJK JP",
            "IPAGothic",
            "VL Gothic",
            NULL
        };

        for (int i = 0; preferred_mono[i] && mono_path[0] == '\0'; i++) {
            char cmd[256];
            snprintf(cmd, sizeof(cmd),
                     "fc-match \"%s\" file | sed 's/.*file=//;s/:.*//'", preferred_mono[i]);
            FILE *fc = popen(cmd, "r");
            if (fc) {
                char tmp[MAX_PATH] = {0};
                if (fgets(tmp, sizeof(tmp), fc)) {
                    size_t len = strlen(tmp);
                    if (len > 0 && tmp[len-1] == '\n') tmp[len-1] = '\0';
                    char verify[256];
                    snprintf(verify, sizeof(verify),
                             "fc-match \"%s\" family | grep -i \"%s\" > /dev/null 2>&1",
                             preferred_mono[i], preferred_mono[i]);
                    if (system(verify) == 0)
                        snprintf(mono_path, sizeof(mono_path), "%s", tmp);
                }
                pclose(fc);
            }
        }

        /* Fallback: any monospace with Japanese */
        if (mono_path[0] == '\0') {
            FILE *fc = popen("fc-match monospace:lang=ja file | sed 's/.*file=//;s/:.*//'", "r");
            if (fc) {
                if (fgets(mono_path, sizeof(mono_path), fc)) {
                    size_t len = strlen(mono_path);
                    if (len > 0 && mono_path[len-1] == '\n') mono_path[len-1] = '\0';
                }
                pclose(fc);
            }
        }

        /* Last resort: system monospace */
        if (mono_path[0] == '\0') {
            FILE *fc = popen("fc-match monospace file | sed 's/.*file=//;s/:.*//'", "r");
            if (fc) {
                if (fgets(mono_path, sizeof(mono_path), fc)) {
                    size_t len = strlen(mono_path);
                    if (len > 0 && mono_path[len-1] == '\n') mono_path[len-1] = '\0';
                }
                pclose(fc);
            }
        }
    }

    /* Font baking - bake both fonts in one atlas with full Unicode ranges */
    struct nk_font_atlas *atlas;
    nk_sdl_font_stash_begin(&atlas);

    /* Build a glyph range covering:
     * - Basic Latin + Latin Extended
     * - Hiragana, Katakana
     * - CJK Unified Ideographs (common kanji)
     * - Hangul syllables
     * - Cyrillic, Greek, etc.
     * Using nk_font_config to pass custom ranges */
    static const nk_rune glyph_ranges[] = {
        0x0020, 0x00FF, /* Basic Latin + Latin Supplement */
        0x0100, 0x024F, /* Latin Extended A/B */
        0x0370, 0x03FF, /* Greek */
        0x0400, 0x04FF, /* Cyrillic */
        0x3000, 0x303F, /* CJK Symbols and Punctuation */
        0x3040, 0x309F, /* Hiragana */
        0x30A0, 0x30FF, /* Katakana */
        0x31F0, 0x31FF, /* Katakana Phonetic Extensions */
        0x4E00, 0x9FFF, /* CJK Unified Ideographs (common kanji) */
        0xAC00, 0xD7AF, /* Hangul Syllables */
        0xFF00, 0xFFEF, /* Halfwidth and Fullwidth Forms */
        0
    };

    struct nk_font_config cfg = nk_font_config(15.0f);
    cfg.range = glyph_ranges;
    cfg.oversample_h = 2;
    cfg.oversample_v = 2;

    struct nk_font_config cfg_mono = nk_font_config(14.0f);
    cfg_mono.range = glyph_ranges;
    cfg_mono.oversample_h = 2;
    cfg_mono.oversample_v = 2;

    struct nk_font *font = NULL;
    if (strlen(font_path) > 0)
        font = nk_font_atlas_add_from_file(atlas, font_path, 15.0f, &cfg);
    if (!font)
        font = nk_font_atlas_add_default(atlas, 15.0f, NULL);

    struct nk_font *font_mono = NULL;
    if (strlen(mono_path) > 0)
        font_mono = nk_font_atlas_add_from_file(atlas, mono_path, 14.0f, &cfg_mono);
    if (!font_mono)
        font_mono = font; /* fallback to regular if mono not found */

    nk_sdl_font_stash_end();
    nk_style_set_font(ctx, &font->handle);

    fprintf(stderr, "UI font:   %s\n", strlen(font_path)  ? font_path  : "(built-in)");
    fprintf(stderr, "Mono font: %s\n", strlen(mono_path) ? mono_path : "(fallback)");

    /* ── Warm dark palette ──────────────────────────────────────
     * bg:        #1c1a1a   primary background
     * bg2:       #262220   cards / secondary blocks
     * text:      #ede0d0   primary text (soft cream)
     * nk_rgb(0x9a, 0x8c, 0x80):     #9a8c80   secondary text / labels
     * accent:    #c8a978   links, highlights, hover
     * accent2:   #a08858   pressed / active accent
     * border:    #3a3430   subtle borders
     * ──────────────────────────────────────────────────────── */
    struct nk_color bg      = nk_rgb(0x1c, 0x1a, 0x1a);
    struct nk_color bg2     = nk_rgb(0x26, 0x22, 0x20);
    struct nk_color text1   = nk_rgb(0xed, 0xe0, 0xd0);
    struct nk_color accent  = nk_rgb(0xc8, 0xa9, 0x78);
    struct nk_color accent2 = nk_rgb(0xa0, 0x88, 0x58);
    struct nk_color border  = nk_rgb(0x3a, 0x34, 0x30);

    nk_style_default(ctx);

    /* Window */
    ctx->style.window.background       = bg;
    ctx->style.window.fixed_background = nk_style_item_color(bg);
    ctx->style.window.border_color     = border;
    ctx->style.window.padding          = nk_vec2(16, 12);
    ctx->style.window.spacing          = nk_vec2(6, 6);

    /* Labels */
    ctx->style.text.color = text1;

    /* Buttons — accent colored */
    ctx->style.button.normal      = nk_style_item_color(accent2);
    ctx->style.button.hover       = nk_style_item_color(accent);
    ctx->style.button.active      = nk_style_item_color(nk_rgb(0x80, 0x68, 0x40));
    ctx->style.button.text_normal = nk_rgb(0x1c, 0x1a, 0x1a);
    ctx->style.button.text_hover  = nk_rgb(0x1c, 0x1a, 0x1a);
    ctx->style.button.text_active = nk_rgb(0x1c, 0x1a, 0x1a);
    ctx->style.button.border      = 0;
    ctx->style.button.rounding    = 4;
    ctx->style.button.padding     = nk_vec2(8, 6);

    /* Edit fields */
    ctx->style.edit.normal       = nk_style_item_color(bg2);
    ctx->style.edit.hover        = nk_style_item_color(nk_rgb(0x2e, 0x28, 0x24));
    ctx->style.edit.active       = nk_style_item_color(nk_rgb(0x30, 0x2a, 0x26));
    ctx->style.edit.text_normal  = text1;
    ctx->style.edit.text_hover   = text1;
    ctx->style.edit.text_active  = text1;
    ctx->style.edit.cursor_normal = accent;
    ctx->style.edit.cursor_hover  = accent;
    ctx->style.edit.cursor_text_normal = bg;
    ctx->style.edit.cursor_text_hover  = bg;
    ctx->style.edit.selected_normal    = accent2;
    ctx->style.edit.selected_hover     = accent;
    ctx->style.edit.selected_text_normal = bg;
    ctx->style.edit.selected_text_hover  = bg;
    ctx->style.edit.border       = 1;
    ctx->style.edit.border_color = border;
    ctx->style.edit.rounding     = 3;
    ctx->style.edit.padding      = nk_vec2(6, 5);

    /* Combo / dropdown */
    ctx->style.combo.normal        = nk_style_item_color(bg2);
    ctx->style.combo.hover         = nk_style_item_color(nk_rgb(0x2e, 0x28, 0x24));
    ctx->style.combo.active        = nk_style_item_color(nk_rgb(0x30, 0x2a, 0x26));
    ctx->style.combo.label_normal  = text1;
    ctx->style.combo.label_hover   = text1;
    ctx->style.combo.label_active  = text1;
    ctx->style.combo.border        = 1;
    ctx->style.combo.border_color  = border;
    ctx->style.combo.rounding      = 3;
    /* combo arrow button */
    ctx->style.combo.button.normal     = nk_style_item_color(bg2);
    ctx->style.combo.button.hover      = nk_style_item_color(accent2);
    ctx->style.combo.button.active     = nk_style_item_color(accent);
    ctx->style.combo.button.text_normal = accent;
    ctx->style.combo.button.text_hover  = bg;
    ctx->style.combo.button.text_active = bg;

    /* Checkbox */
    ctx->style.checkbox.normal       = nk_style_item_color(bg2);
    ctx->style.checkbox.hover        = nk_style_item_color(nk_rgb(0x2e, 0x28, 0x24));
    ctx->style.checkbox.active       = nk_style_item_color(nk_rgb(0x30, 0x2a, 0x26));
    ctx->style.checkbox.cursor_normal = nk_style_item_color(accent);
    ctx->style.checkbox.cursor_hover  = nk_style_item_color(accent2);
    ctx->style.checkbox.text_normal   = text1;
    ctx->style.checkbox.text_hover    = text1;
    ctx->style.checkbox.text_active   = text1;
    ctx->style.checkbox.border_color  = border;
    ctx->style.checkbox.padding       = nk_vec2(3, 3);

    /* Scrollbar (for combo dropdowns) */
    ctx->style.scrollv.normal   = nk_style_item_color(bg2);
    ctx->style.scrollv.hover    = nk_style_item_color(bg2);
    ctx->style.scrollv.active   = nk_style_item_color(bg2);
    ctx->style.scrollv.cursor_normal = nk_style_item_color(accent2);
    ctx->style.scrollv.cursor_hover  = nk_style_item_color(accent);
    ctx->style.scrollv.cursor_active = nk_style_item_color(accent);
    ctx->style.scrollv.border_color  = border;



    /* Main loop */
    int running = 1;
    while (running) {
        SDL_Event evt;
        nk_input_begin(ctx);
        if (SDL_WaitEventTimeout(&evt, 32)) {
            do {
                if (evt.type == SDL_QUIT) running = 0;
                if (evt.type == SDL_WINDOWEVENT &&
                    evt.window.event == SDL_WINDOWEVENT_CLOSE) running = 0;
                nk_sdl_handle_event(&evt);
            } while (SDL_PollEvent(&evt));
        }
        nk_input_end(ctx);

        /* Check if async picker finished */
        if (g_pick_target != PICK_NONE) {
            char picked[MAX_PATH] = {0};
            int res = picker_check(picked, sizeof(picked));
            if (res == 2) { /* user picked a file */
                switch (g_pick_target) {
                    case PICK_EXE:
                        snprintf(s.exe_path, sizeof(s.exe_path), "%s", picked);
                        break;
                    case PICK_ICON:
                        snprintf(s.icon_path, sizeof(s.icon_path), "%s", picked);
                        break;
                    case PICK_PROTON_DIR:
                        snprintf(s.custom_proton, sizeof(s.custom_proton), "%s", picked);
                        break;
                    case PICK_WINEPREFIX:
                        snprintf(s.wineprefix, sizeof(s.wineprefix), "%s", picked);
                        break;
                    case PICK_NATIVE_BIN:
                        snprintf(n.bin_path, sizeof(n.bin_path), "%s", picked);
                        break;
                    case PICK_NATIVE_ICON:
                        snprintf(n.icon_path, sizeof(n.icon_path), "%s", picked);
                        break;
                    default: break;
                }
                g_pick_target = PICK_NONE;
            } else if (res == 1) { /* cancelled */
                g_pick_target = PICK_NONE;
            }
        }

        draw_ui(ctx, &s, &n, font, font_mono);

        int w, h;
        SDL_GetWindowSize(win, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0x1c/255.0f, 0x1a/255.0f, 0x1a/255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        nk_sdl_render(NK_ANTI_ALIASING_ON);
        SDL_GL_SwapWindow(win);
    }

    nk_sdl_shutdown();
    SDL_GL_DeleteContext(glctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
