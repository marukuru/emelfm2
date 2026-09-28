/* Internal viewer highlighting. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef E2_VIEWER_SYNTAX_H
#define E2_VIEWER_SYNTAX_H
#include <gtk/gtk.h>

G_BEGIN_DECLS
GtkTextBuffer *e2_viewer_syntax_buffer_new (void);
/* Text must be the decoded UTF-8 content already installed in buffer. */
void e2_viewer_syntax_update (GtkTextBuffer *buffer, const gchar *filename,
    const gchar *text, gboolean enabled, gboolean dark);
#if GTK_MAJOR_VERSION < 3
gboolean e2_source_highlight (GtkTextBuffer *buffer, const gchar *filename,
    const gchar *text, const gchar *mime, const gchar *data_dir, gboolean dark);
void e2_source_highlight_clear (GtkTextBuffer *buffer);
#endif
G_END_DECLS
#endif
