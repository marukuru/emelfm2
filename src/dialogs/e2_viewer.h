/* Internal file viewer presentation. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef E2_VIEWER_H
#define E2_VIEWER_H
#include <gtk/gtk.h>

typedef struct _E2_Viewer E2_Viewer;
/* Takes ownership of GLib-allocated bytes and copies filename
 * (in the filesystem encoding). */
E2_Viewer *e2_viewer_new (gpointer bytes, gsize length, const gchar *filename);
/* Destroy the attached widgets before freeing their viewer state. */
void e2_viewer_free (E2_Viewer *viewer);
/* Returns a new reference; the caller must unref it. */
GtkTextBuffer *e2_viewer_buffer (E2_Viewer *viewer);
/* Borrowed encoding name; do not free it. */
const gchar *e2_viewer_encoding (E2_Viewer *viewer);
gboolean e2_viewer_is_art (E2_Viewer *viewer);
GtkWidget *e2_viewer_scrolled (E2_Viewer *viewer, GtkWidget *box);
void e2_viewer_set_font (E2_Viewer *viewer, GtkWidget *textview, gint *width, gint *height);
void e2_viewer_attach (E2_Viewer *viewer, GtkWidget *textview, GtkWidget *box);
void e2_viewer_add_actions (E2_Viewer *viewer, GtkWidget *actions);
#endif
