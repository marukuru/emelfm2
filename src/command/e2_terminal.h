/* Optional embedded terminals. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef E2_TERMINAL_H
#define E2_TERMINAL_H
#include "emelfm2.h"
#ifdef E2_VTE
GtkWidget *e2_terminal_wrap_output (GtkWidget *output);
void e2_terminal_actions_register (void);
void e2_terminal_options_register (void);
gboolean e2_terminal_has_focus (void);
gboolean e2_terminal_window_key (GtkWidget *window, GdkEventKey *event, gpointer data);
gboolean e2_terminal_confirm_shutdown (void);
void e2_terminal_shutdown (void);
#endif
#endif
