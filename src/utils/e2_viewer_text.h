/* Internal viewer decoding. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef E2_VIEWER_TEXT_H
#define E2_VIEWER_TEXT_H
#include <glib.h>

typedef enum { E2_VIEWER_PLAIN, E2_VIEWER_AMIGA, E2_VIEWER_PC } E2_ViewerArt;
typedef struct
{
    gchar *text;                 /* UTF-8; caller frees with g_free(). */
    const gchar *encoding;      /* Static name or the borrowed override. */
    E2_ViewerArt art;
    gboolean damaged, binary;
} E2_ViewerText;
typedef struct { gint start, end; gchar *uri; } E2_ViewerLink;

/* filename is in the filesystem encoding; patterns are UTF-8 and separated by ';'. */
gboolean e2_viewer_matches_extensions (const gchar *filename, const gchar *patterns);
E2_ViewerText e2_viewer_decode (const guint8 *bytes, gsize length,
    gboolean detect_art, const gchar *encoding);
GPtrArray *e2_viewer_find_links (const gchar *text);
gboolean e2_viewer_valid_uri (const gchar *uri);
void e2_viewer_link_free (gpointer link);
#endif
