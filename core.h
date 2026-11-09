/* SPDX-License-Identifier: MIT */
/*
 * asc - A Shortcut Creator
 * core.h - shortcut creation logic. No SDL, no Nuklear.
 */
#ifndef ASC_CORE_H
#define ASC_CORE_H

#include "state.h"

/* Scan Steam / compatibilitytools.d for Proton installs.
 * Fills s->proton_list / proton_labels / proton_count,
 * always appending "Custom..." as the last entry. */
void        detect_proton          (AppState *s);

/* Write the .desktop file (Wine/umu tab) and install the icon.
 * Result is reported through s->status_msg / status_ok. */
void        create_shortcut        (AppState *s);

/* Write the .desktop file (Native/AppImage tab) and install the icon.
 * Result is reported through n->status_msg / status_ok. */
void        create_native_shortcut (NativeState *n);

#endif /* ASC_CORE_H */
