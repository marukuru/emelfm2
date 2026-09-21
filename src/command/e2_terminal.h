/* Optional embedded terminals. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef E2_TERMINAL_H
#define E2_TERMINAL_H
#include "emelfm2.h"
#ifdef E2_VTE
typedef struct _E2_TerminalWorkspace E2_TerminalWorkspace;
GtkWidget *e2_terminal_wrap_output (GtkWidget *output);
E2_TerminalWorkspace *e2_terminal_workspace_current (void);
E2_TerminalWorkspace *e2_terminal_workspace_new (void);
void e2_terminal_workspace_select (E2_TerminalWorkspace *space);
gboolean e2_terminal_workspace_can_close (E2_TerminalWorkspace *space);
void e2_terminal_workspace_close (E2_TerminalWorkspace *space);
void e2_terminal_workspace_merge (E2_TerminalWorkspace *space);
void e2_terminal_select_pane (void);
void e2_terminal_sync_layout (void);
void e2_terminal_restore_tools (void);
void e2_terminal_select_output (GtkWidget *output);
void e2_terminal_actions_register (void);
void e2_terminal_options_register (void);
gboolean e2_terminal_has_focus (void);
gboolean e2_terminal_show_menu (void);
gboolean e2_terminal_window_key (GtkWidget *window, GdkEventKey *event, gpointer data);
gboolean e2_terminal_confirm_shutdown (void);
void e2_terminal_shutdown (void);
#endif
#endif
