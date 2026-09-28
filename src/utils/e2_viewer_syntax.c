/* Internal viewer highlighting. SPDX-License-Identifier: GPL-3.0-or-later */
#include "e2_viewer_syntax.h"
#include <gio/gio.h>
#include <string.h>
#if GTK_MAJOR_VERSION >= 3
#include <gtksourceview/gtksource.h>
#endif

/* Whole-document highlighting is deliberately bounded: log files and minified
 * data must remain practical to view even with highlighting enabled. */
static gboolean suitable_text (const gchar *text)
{
    gsize length = strlen (text);
    if (length > 2 * 1024 * 1024) return FALSE;
    const gchar *line = text;
    for (const gchar *p = text; ; p++)
    {
        if (p - line > 16384) return FALSE;
        if (*p == '\0') break;
        if (*p == '\n') line = p + 1;
    }
    return TRUE;
}

/* AppImages keep the library's data alongside the executable. Normal installs
 * use each library's standard search path. */
static gchar *bundled_data (const gchar *name)
{
    gchar *exe = g_file_read_link ("/proc/self/exe", NULL);
    if (exe == NULL) return NULL;
    gchar *bin = g_path_get_dirname (exe);
    gchar *path = g_build_filename (bin, "..", "share", name, NULL);
    g_free (exe);
    g_free (bin);
    if (g_file_test (path, G_FILE_TEST_IS_DIR)) return path;
    g_free (path);
    return NULL;
}

GtkTextBuffer *e2_viewer_syntax_buffer_new (void)
{
#if GTK_MAJOR_VERSION >= 3
    static gboolean initialized;
    if (!initialized)
    {
        gtk_source_init ();
        gchar *data = bundled_data ("gtksourceview-4");
        if (data != NULL)
        {
            GtkSourceLanguageManager *manager = gtk_source_language_manager_get_default ();
            const gchar * const *old = gtk_source_language_manager_get_search_path (manager);
            guint count = g_strv_length ((gchar **)old);
            gchar **paths = g_new0 (gchar *, count + 2);
            paths[0] = g_build_filename (data, "language-specs", NULL);
            for (guint i = 0; i < count; i++) paths[i + 1] = g_strdup (old[i]);
            gtk_source_language_manager_set_search_path (manager, paths);
            g_strfreev (paths);
            gchar *styles = g_build_filename (data, "styles", NULL);
            gtk_source_style_scheme_manager_prepend_search_path (
                gtk_source_style_scheme_manager_get_default (), styles);
            g_free (styles);
            g_free (data);
        }
        initialized = TRUE;
    }
    GtkSourceBuffer *buffer = gtk_source_buffer_new (NULL);
    gtk_source_buffer_set_max_undo_levels (buffer, 0);
    gtk_source_buffer_set_highlight_matching_brackets (buffer, FALSE);
    gtk_source_buffer_set_highlight_syntax (buffer, FALSE);
    return GTK_TEXT_BUFFER (buffer);
#else
    return gtk_text_buffer_new (NULL);
#endif
}

void e2_viewer_syntax_update (GtkTextBuffer *buffer, const gchar *filename,
    const gchar *text, gboolean enabled, gboolean dark)
{
    enabled = enabled && suitable_text (text);
    /* Content sniffing also recognises scripts with a shebang and no suffix.
     * Filename mappings have precedence in both backends. */
    gchar *type = enabled ? g_content_type_guess (NULL, (const guchar *)text,
        MIN (strlen (text), 4096), NULL) : NULL;
    gchar *mime = type == NULL ? NULL : g_content_type_get_mime_type (type);
#if GTK_MAJOR_VERSION >= 3
    GtkSourceBuffer *source = GTK_SOURCE_BUFFER (buffer);
    GtkSourceLanguageManager *manager = gtk_source_language_manager_get_default ();
    GtkSourceLanguage *language = enabled ?
        gtk_source_language_manager_guess_language (manager, filename, NULL) : NULL;
    if (enabled && language == NULL)
    {
        gchar *named_type = g_content_type_guess (filename, NULL, 0, NULL);
        gchar *named_mime = g_content_type_get_mime_type (named_type);
        language = gtk_source_language_manager_guess_language (manager, filename, named_mime);
        g_free (named_mime);
        g_free (named_type);
    }
    if (enabled && language == NULL && mime != NULL)
        language = gtk_source_language_manager_guess_language (manager, NULL, mime);
    gtk_source_buffer_set_highlight_syntax (source, FALSE);
    gtk_source_buffer_set_language (source, language);
    gtk_source_buffer_set_style_scheme (source, gtk_source_style_scheme_manager_get_scheme (
        gtk_source_style_scheme_manager_get_default (), dark ? "oblivion" : "classic"));
    if (language != NULL)
    {
        gtk_source_buffer_set_highlight_syntax (source, TRUE);
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds (buffer, &start, &end);
        /* The viewer retains GtkTextView and its custom gutter. Unlike
         * GtkSourceView, it does not request highlighting of visible ranges. */
        gtk_source_buffer_ensure_highlight (source, &start, &end);
    }
#else
    e2_source_highlight_clear (buffer);
    if (enabled)
    {
        gchar *data = bundled_data ("source-highlight");
        if (!e2_source_highlight (buffer, filename, text, mime, data, dark))
            e2_source_highlight_clear (buffer);
        g_free (data);
    }
#endif
    g_free (mime);
    g_free (type);
}
