/* Terminal identity tracking, independent of GTK/VTE. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef E2_TERMINAL_CONTEXT_H
#define E2_TERMINAL_CONTEXT_H
#include <glib.h>

typedef struct _E2_TerminalContext E2_TerminalContext;
E2_TerminalContext *e2_terminal_context_new (const gchar *directory);
void e2_terminal_context_free (E2_TerminalContext *context);
/* title and uri are optional shell reports, never commands to execute. */
void e2_terminal_context_update (E2_TerminalContext *context, GPid foreground,
    const gchar *title, const gchar *uri);
gchar *e2_terminal_context_label (E2_TerminalContext *context);
#endif
