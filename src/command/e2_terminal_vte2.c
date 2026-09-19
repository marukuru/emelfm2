/* GTK 2 / VTE 0.28 adapter. SPDX-License-Identifier: GPL-3.0-or-later */
#include "emelfm2.h"
#ifdef E2_VTE2
#include "e2_terminal_backend.h"
#include <vte/vte.h>

typedef struct { E2_TerminalExited callback; gpointer data; } ExitData;
static void exited_cb (VteTerminal *terminal, ExitData *exit)
{
    /* VTE 0.28's signal has no status argument. VTE owns the child watch. */
    exit->callback (0, FALSE, exit->data);
}
static void free_exit (gpointer data, GClosure *closure) { g_free (data); }
GtkWidget *e2_terminal_backend_new (E2_TerminalExited callback, gpointer data)
{
    GtkWidget *terminal = vte_terminal_new ();
    ExitData *exit = g_new (ExitData, 1);
    exit->callback = callback; exit->data = data;
    g_signal_connect_data (terminal, "child-exited", G_CALLBACK (exited_cb),
        exit, free_exit, 0);
    return terminal;
}
void e2_terminal_backend_spawn (GtkWidget *terminal, const gchar *directory,
    gchar **argv, GCancellable *cancel, E2_TerminalSpawned callback, gpointer data)
{
    GError *error = NULL;
    GPid pid = -1;
    if (!g_cancellable_set_error_if_cancelled (cancel, &error))
        vte_terminal_fork_command_full (VTE_TERMINAL (terminal), VTE_PTY_DEFAULT,
            directory, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &pid, &error);
    callback (pid, error, data);
    if (error != NULL) g_error_free (error);
}
GtkAdjustment *e2_terminal_backend_adjustment (GtkWidget *terminal)
{ return vte_terminal_get_adjustment (VTE_TERMINAL (terminal)); }
void e2_terminal_backend_configure (GtkWidget *terminal, gint scrollback,
    const gchar *font, const gchar *foreground, const gchar *background)
{
    VteTerminal *vte = VTE_TERMINAL (terminal);
    GdkColor fg, bg;
    if (gdk_color_parse (foreground, &fg) && gdk_color_parse (background, &bg))
        vte_terminal_set_colors (vte, &fg, &bg, NULL, 0);
    vte_terminal_set_font_from_string (vte, font);
    vte_terminal_set_scrollback_lines (vte, scrollback);
    vte_terminal_set_scroll_on_keystroke (vte, TRUE);
    vte_terminal_set_scroll_on_output (vte, FALSE);
}
void e2_terminal_backend_send (GtkWidget *terminal, const gchar *text)
{ vte_terminal_feed_child (VTE_TERMINAL (terminal), text, -1); }
gchar *e2_terminal_backend_text (GtkWidget *terminal)
{ return vte_terminal_get_text (VTE_TERMINAL (terminal), NULL, NULL, NULL); }
void e2_terminal_backend_insert (GtkWidget *terminal, const gchar *text)
{ vte_terminal_feed_child (VTE_TERMINAL (terminal), text, -1); }
void e2_terminal_backend_copy (GtkWidget *terminal)
{ vte_terminal_copy_clipboard (VTE_TERMINAL (terminal)); }
void e2_terminal_backend_paste (GtkWidget *terminal)
{ vte_terminal_paste_clipboard (VTE_TERMINAL (terminal)); }
gboolean e2_terminal_backend_has_selection (GtkWidget *terminal)
{ return vte_terminal_get_has_selection (VTE_TERMINAL (terminal)); }
#endif
