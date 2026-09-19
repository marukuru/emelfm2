/* Private adapter contract: no VTE types escape this boundary.
 * All calls and callbacks run on the GTK main thread.
 * spawn calls its callback exactly once (possibly before returning). */
#ifndef E2_TERMINAL_BACKEND_H
#define E2_TERMINAL_BACKEND_H
#include <gtk/gtk.h>
#include <gio/gio.h>
typedef void (*E2_TerminalSpawned) (GPid pid, const GError *error, gpointer data);
typedef void (*E2_TerminalExited) (gint status, gboolean known, gpointer data);
GtkWidget *e2_terminal_backend_new (E2_TerminalExited exited, gpointer data);
void e2_terminal_backend_spawn (GtkWidget *terminal, const gchar *directory,
    gchar **argv, GCancellable *cancel, E2_TerminalSpawned callback, gpointer data);
void e2_terminal_backend_configure (GtkWidget *terminal, gint scrollback,
    const gchar *font, const gchar *foreground, const gchar *background);
GtkAdjustment *e2_terminal_backend_adjustment (GtkWidget *terminal);
void e2_terminal_backend_send (GtkWidget *terminal, const gchar *text);
gchar *e2_terminal_backend_text (GtkWidget *terminal);
void e2_terminal_backend_insert (GtkWidget *terminal, const gchar *text);
void e2_terminal_backend_copy (GtkWidget *terminal);
void e2_terminal_backend_paste (GtkWidget *terminal);
gboolean e2_terminal_backend_has_selection (GtkWidget *terminal);
#endif
