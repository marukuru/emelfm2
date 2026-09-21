/* Shared log/terminal search. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef E2_TERMINAL_SEARCH_H
#define E2_TERMINAL_SEARCH_H
#include <gtk/gtk.h>
GtkWidget *e2_terminal_search_new (void);
void e2_terminal_search_target (GtkWidget *bar, GtkWidget *target);
void e2_terminal_search_show (GtkWidget *bar, GtkWidget *target);
#endif
