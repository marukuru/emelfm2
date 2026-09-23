/* Optional presentation layer; no effect on widget layout or actions.
 * Copyright (C) 2026 emelFM2 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef __E2_MODERN_UI_H__
#define __E2_MODERN_UI_H__

#include "emelfm2.h"

#ifdef E2_MODERN_UI
void e2_modern_ui_init (void);
gboolean e2_modern_ui_enabled (void);
/* Borrowed pixbuf, like e2_icons_get_puxbuf(); NULL means use the usual icon. */
GdkPixbuf *e2_modern_ui_icon (const gchar *name, gint width, gint height);
void e2_modern_ui_clear_icons (void);
#else
static inline void e2_modern_ui_init (void) {}
static inline gboolean e2_modern_ui_enabled (void) { return FALSE; }
static inline GdkPixbuf *e2_modern_ui_icon (const gchar *name, gint width, gint height) { return NULL; }
static inline void e2_modern_ui_clear_icons (void) {}
#endif

#endif
