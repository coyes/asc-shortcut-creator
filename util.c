/* SPDX-License-Identifier: MIT */
/*
 * asc - A Shortcut Creator
 * util.c - small process/filesystem helpers.
 */
#include "util.h"
#include "state.h"

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

const char *
get_home (void)
{
    const char *home = getenv ("HOME");
    if (home == NULL) {
        struct passwd *pw = getpwuid (getuid ());
        if (pw != NULL)
            home = pw->pw_dir;
    }
    return home != NULL ? home : "/tmp";
}

int
mkdirp (const char *path)
{
    char tmp[MAX_PATH * 2];
    snprintf (tmp, sizeof (tmp), "%s", path);
    size_t len = strlen (tmp);
    if (len == 0)
        return -1;
    if (tmp[len - 1] == '/')
        tmp[len - 1] = '\0';

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir (tmp, 0755);
            *p = '/';
        }
    }
    return mkdir (tmp, 0755);
}
