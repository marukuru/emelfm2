/* GTK 2 / VTE 0.28 adapter. SPDX-License-Identifier: GPL-3.0-or-later */
#include "emelfm2.h"
#ifdef E2_VTE2
#include "e2_terminal_backend.h"
#include <vte/vte.h>
#include <unistd.h>

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
    gulong handler = g_signal_connect_data (terminal, "child-exited", G_CALLBACK (exited_cb),
        exit, free_exit, 0);
    g_object_set_data (G_OBJECT (terminal), "e2-exit-handler", GSIZE_TO_POINTER (handler));
    return terminal;
}
void e2_terminal_backend_disconnect (GtkWidget *terminal)
{
    gulong handler = GPOINTER_TO_SIZE (g_object_get_data (G_OBJECT (terminal), "e2-exit-handler"));
    if (handler != 0)
    {
        g_signal_handler_disconnect (terminal, handler);
        g_object_set_data (G_OBJECT (terminal), "e2-exit-handler", NULL);
    }
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
GPid e2_terminal_backend_foreground_pid (GtkWidget *terminal)
{
    VtePty *pty = vte_terminal_get_pty_object (VTE_TERMINAL (terminal));
    return pty == NULL ? -1 : tcgetpgrp (vte_pty_get_fd (pty));
}
const gchar *e2_terminal_backend_title (GtkWidget *terminal)
{ return vte_terminal_get_window_title (VTE_TERMINAL (terminal)); }
const gchar *e2_terminal_backend_directory_uri (GtkWidget *terminal)
{ return NULL; } /* OSC 7 metadata is unavailable in VTE 0.28. */
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
gboolean e2_terminal_backend_search (GtkWidget *terminal, const gchar *text, gboolean backwards)
{
    if (g_strcmp0 (g_object_get_data (G_OBJECT (terminal), "e2-search-text"), text))
    {
        gchar *pattern = g_regex_escape_string (text, -1);
        GRegex *regex = *text ? g_regex_new (pattern, G_REGEX_MULTILINE | G_REGEX_CASELESS, 0, NULL) : NULL;
        vte_terminal_search_set_gregex (VTE_TERMINAL (terminal), regex);
        if (regex != NULL) g_regex_unref (regex);
        g_free (pattern);
        g_object_set_data_full (G_OBJECT (terminal), "e2-search-text", g_strdup (text), g_free);
        vte_terminal_search_set_wrap_around (VTE_TERMINAL (terminal), TRUE);
    }
    return *text && (backwards ? vte_terminal_search_find_previous (VTE_TERMINAL (terminal))
        : vte_terminal_search_find_next (VTE_TERMINAL (terminal)));
}
#endif
