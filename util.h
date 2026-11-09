/* SPDX-License-Identifier: MIT */
/*
 * asc - A Shortcut Creator
 * util.h - filesystem helpers.
 */
#ifndef ASC_UTIL_H
#define ASC_UTIL_H

#include <stddef.h>

/* $HOME with passwd fallback. Never returns NULL. */
const char *get_home (void);

/* Create directory and parents (like mkdir -p). */
int         mkdirp   (const char *path);

#endif /* ASC_UTIL_H */
