/* Exercise real syntax libraries, Unicode offsets, and tag coexistence. */
#include "e2_viewer_syntax.h"
#include <string.h>

static gboolean colored (GtkTextBuffer *buffer, const gchar *text, const gchar *needle)
{
    const gchar *where = strstr (text, needle);
    g_assert_nonnull (where);
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_offset (buffer, &iter, g_utf8_pointer_to_offset (text, where));
    GSList *tags = gtk_text_iter_get_tags (&iter);
    gboolean result = FALSE;
    for (GSList *p = tags; p != NULL; p = p->next)
    {
        gboolean set;
        g_object_get (p->data, "foreground-set", &set, NULL);
        result |= set;
    }
    g_slist_free (tags);
    return result;
}
static void check (const gchar *filename, const gchar *text, const gchar *token)
{
    g_test_message ("%s: %s", filename, token);
    GtkTextBuffer *buffer = e2_viewer_syntax_buffer_new ();
    gtk_text_buffer_set_text (buffer, text, -1);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds (buffer, &start, &end);
    GtkTextTag *match = gtk_text_buffer_create_tag (buffer, "matched", "background", "yellow", NULL);
    gtk_text_buffer_apply_tag (buffer, match, &start, &end);
    e2_viewer_syntax_update (buffer, filename, text, TRUE, TRUE);
    g_assert_true (colored (buffer, text, token));
    gchar *actual = gtk_text_buffer_get_text (buffer, &start, &end, TRUE);
    g_assert_cmpstr (actual, ==, text);
    g_free (actual);
    g_assert_true (gtk_text_iter_has_tag (&start, match));
    e2_viewer_syntax_update (buffer, filename, text, FALSE, TRUE);
    g_assert_false (colored (buffer, text, token));
    g_assert_true (gtk_text_iter_has_tag (&start, match));
    e2_viewer_syntax_update (buffer, filename, text, TRUE, FALSE);
    g_assert_true (colored (buffer, text, token));
    /* A refresh must discard old syntax ranges. */
    gtk_text_buffer_set_text (buffer, "plain words", -1);
    e2_viewer_syntax_update (buffer, "unknown.zzz", "plain words", TRUE, FALSE);
    g_assert_false (colored (buffer, "plain words", "words"));
    g_object_unref (buffer);
}
static void languages (void)
{
    check ("sample.c", "/* 日本語 🌍 */\nint main(void) { return 42; }", "return");
    check ("sample.c", "/* first\nsecond comment */\nint n;\n", "second");
    check ("sample.py", "# 日本語\ndef greet():\n    return '世界'\n", "return");
    check ("script", "#!/usr/bin/env python3\nif True:\n    print('世界')\n", "if");
    check ("script", "#!/bin/sh\nif true; then echo hello; fi\n", "then");
    check ("Makefile", "# example\nall: main.o\n\tcc main.o -o main\n", "# example");
    check ("sample.json", "{\"hello\": \"世界\", \"count\": 42}\n", "42");
    check ("sample.xml", "<?xml version=\"1.0\"?>\n<root>日本語</root>", "root");
}
static void fallbacks (void)
{
    GtkTextBuffer *buffer = e2_viewer_syntax_buffer_new ();
    const gchar *plain = "ordinary words";
    gtk_text_buffer_set_text (buffer, plain, -1);
    e2_viewer_syntax_update (buffer, "notes.txt", plain, TRUE, TRUE);
    g_assert_false (colored (buffer, plain, "ordinary"));
    GString *large = g_string_new ("int main(void) { return 0; }\n");
    for (guint i = 0; i < 80000; i++) g_string_append (large, "/* repeated comment line */\n");
    gtk_text_buffer_set_text (buffer, large->str, -1);
    e2_viewer_syntax_update (buffer, "large.c", large->str, TRUE, TRUE);
    g_assert_false (colored (buffer, large->str, "return"));
    g_string_assign (large, "int n; /*");
    for (guint i = 0; i < 17000; i++) g_string_append_c (large, 'x');
    gtk_text_buffer_set_text (buffer, large->str, -1);
    e2_viewer_syntax_update (buffer, "long.c", large->str, TRUE, TRUE);
    g_assert_false (colored (buffer, large->str, "int"));
    g_string_free (large, TRUE);
    g_object_unref (buffer);
}
int main (int argc, char **argv)
{
    gtk_test_init (&argc, &argv, NULL);
    g_test_add_func ("/viewer/syntax/languages", languages);
    g_test_add_func ("/viewer/syntax/fallbacks", fallbacks);
    return g_test_run ();
}
