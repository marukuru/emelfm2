/* GTK 3 / VTE >= 0.48 adapter. SPDX-License-Identifier: GPL-3.0-or-later */
#include "emelfm2.h"
#ifdef E2_VTE3
#include "e2_terminal_backend.h"
#include <vte/vte.h>

typedef struct { E2_TerminalExited callback; gpointer data; } ExitData;
typedef struct { E2_TerminalSpawned callback; gpointer data; } SpawnData;
static void exited_cb (VteTerminal *terminal, gint status, ExitData *exit)
{ exit->callback (status, TRUE, exit->data); }
static void free_exit (gpointer data, GClosure *closure) { g_free (data); }
static void spawned_cb (VteTerminal *terminal, GPid pid, GError *error, gpointer data)
{
    SpawnData *spawn = data;
    /* terminal can be NULL if the widget was destroyed while spawning. */
    spawn->callback (pid, error, spawn->data);
    g_free (spawn);
}
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
    SpawnData *spawn = g_new (SpawnData, 1);
    spawn->callback = callback; spawn->data = data;
    vte_terminal_spawn_async (VTE_TERMINAL (terminal), VTE_PTY_DEFAULT,
        directory, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL,
        -1, cancel, spawned_cb, spawn);
}
GtkAdjustment *e2_terminal_backend_adjustment (GtkWidget *terminal)
{ return gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (terminal)); }
void e2_terminal_backend_configure (GtkWidget *terminal, gint scrollback,
    const gchar *font, const gchar *foreground, const gchar *background)
{
    VteTerminal *vte = VTE_TERMINAL (terminal);
    GdkRGBA fg, bg;
    if (gdk_rgba_parse (&fg, foreground) && gdk_rgba_parse (&bg, background))
        vte_terminal_set_colors (vte, &fg, &bg, NULL, 0);
    PangoFontDescription *desc = pango_font_description_from_string (font);
    vte_terminal_set_font (vte, desc);
    pango_font_description_free (desc);
    vte_terminal_set_scrollback_lines (vte, scrollback);
    vte_terminal_set_scroll_on_keystroke (vte, TRUE);
    vte_terminal_set_scroll_on_output (vte, FALSE);
}
void e2_terminal_backend_send (GtkWidget *terminal, const gchar *text)
{ vte_terminal_feed_child (VTE_TERMINAL (terminal), text, -1); }
gchar *e2_terminal_backend_text (GtkWidget *terminal)
{ return vte_terminal_get_text (VTE_TERMINAL (terminal), NULL, NULL, NULL); }
void e2_terminal_backend_insert (GtkWidget *terminal, const gchar *text)
{
#if VTE_CHECK_VERSION(0, 68, 0)
    vte_terminal_paste_text (VTE_TERMINAL (terminal), text);
#else
    vte_terminal_feed_child (VTE_TERMINAL (terminal), text, -1);
#endif
}
void e2_terminal_backend_copy (GtkWidget *terminal)
{
#if VTE_CHECK_VERSION(0, 50, 0)
    vte_terminal_copy_clipboard_format (VTE_TERMINAL (terminal), VTE_FORMAT_TEXT);
#else
    vte_terminal_copy_clipboard (VTE_TERMINAL (terminal));
#endif
}
void e2_terminal_backend_paste (GtkWidget *terminal)
{ vte_terminal_paste_clipboard (VTE_TERMINAL (terminal)); }
gboolean e2_terminal_backend_has_selection (GtkWidget *terminal)
{ return vte_terminal_get_has_selection (VTE_TERMINAL (terminal)); }
#endif
