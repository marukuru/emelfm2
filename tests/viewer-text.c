#include "e2_viewer_text.h"
#include <string.h>
static E2_ViewerText decode (const gchar *bytes, gsize length)
{ return e2_viewer_decode ((const guint8 *)bytes, length, TRUE, NULL); }
int main (void)
{
    const gchar pc[] = "\xc9\xcd\xcd\xcd\xbb\n\xba\xb0\xb1\xb2\xba\n\xc8\xcd\xcd\xcd\xbc\r\n";
    E2_ViewerText text = decode (pc, sizeof (pc)-1);
    g_assert_cmpstr (text.encoding, ==, "CP437");
    g_assert_cmpint (text.art, ==, E2_VIEWER_PC);
    g_assert_nonnull (strstr (text.text, "╔═══╗"));
    g_assert_null (strchr (text.text, '\r'));
    g_free (text.text);
    const gchar amiga[] = "\xc6\xd8\xd8\xd8:........:\xd8\xd8\xd8";
    text = decode (amiga, sizeof (amiga)-1);
    g_assert_cmpstr (text.encoding, ==, "ISO-8859-1");
    g_assert_cmpstr (text.text, ==, "ÆØØØ:........:ØØØ");
    g_assert_cmpint (text.art, ==, E2_VIEWER_AMIGA); g_free (text.text);
    const gchar *unicode = "Hello 世界 ░▒▓█╔═╗\r\n";
    const gchar *encodings[] = {"UTF-8", "UTF-16LE", "UTF-16BE", "UTF-32LE", "UTF-32BE"};
    for (guint i = 0; i < G_N_ELEMENTS (encodings); i++)
    {
        gchar *with_bom = g_strconcat ("\xef\xbb\xbf", unicode, NULL);
        gsize length;
        gchar *bytes = g_convert (with_bom, -1, encodings[i], "UTF-8", NULL, &length, NULL);
        text = decode (bytes, length);
        g_assert_cmpstr (text.encoding, ==, encodings[i]);
        g_assert_cmpstr (text.text, ==, "Hello 世界 ░▒▓█╔═╗\n");
        g_assert_cmpint (text.art, ==, E2_VIEWER_PC);
        g_assert_false (text.damaged); g_assert_false (text.binary);
        g_free (text.text); g_free (bytes); g_free (with_bom);
        bytes = g_convert ("abcd", -1, encodings[i], "UTF-8", NULL, &length, NULL);
        text = decode (bytes, length);
        g_assert_cmpstr (text.text, ==, "abcd");
        g_assert_cmpstr (text.encoding, ==, encodings[i]);
        g_free (text.text); g_free (bytes);
    }
    text = decode ("Ordinary text.\nA second line.\n", 29);
    g_assert_cmpint (text.art, ==, E2_VIEWER_PLAIN); g_free (text.text);
    text = decode ("caf\xe9 \x93quoted\x94", 13);
    g_assert_cmpstr (text.text, ==, "café “quoted”");
    g_assert_cmpint (text.art, ==, E2_VIEWER_PLAIN); g_free (text.text);
    text = decode ("  /\\___/\\\n | |__| |\n \\______//\n", strlen ("  /\\___/\\\n | |__| |\n \\______//\n"));
    g_assert_cmpint (text.art, ==, E2_VIEWER_AMIGA); g_free (text.text);
    text = e2_viewer_decode ((const guint8 *)pc, sizeof (pc)-1, FALSE, NULL);
    g_assert_cmpint (text.art, ==, E2_VIEWER_PLAIN);
    g_assert_cmpstr (text.encoding, ==, "WINDOWS-1252"); g_free (text.text);
    text = e2_viewer_decode ((const guint8 *)"\xb3", 1, TRUE, "CP437");
    g_assert_cmpstr (text.text, ==, "│"); g_free (text.text);
    text = decode ("\xff\xfe" "A\0Z", 5);
    g_assert_true (text.damaged);
    g_assert_cmpstr (text.text, ==, "A�"); g_free (text.text);
    text = decode ("\xef\xbb\xbf" "A\xffZ", 6);
    g_assert_true (text.damaged);
    g_assert_cmpstr (text.text, ==, "A�Z"); g_free (text.text);
    text = decode ("a\0b", 3);
    g_assert_true (text.binary);
    g_assert_cmpstr (text.text, ==, "a␀b"); g_free (text.text);
    text = decode (NULL, 0);
    g_assert_cmpstr (text.text, ==, ""); g_free (text.text);
    gchar sauce[128] = {0}; memcpy (sauce, "SAUCE00", 7); sauce[104] = 1;
    GString *file = g_string_new_len (pc, sizeof (pc)-1);
    g_string_append_c (file, '\x1a'); g_string_append (file, "COMNT");
    for (guint i = 0; i < 64; i++) g_string_append_c (file, 'x');
    g_string_append_len (file, sauce, sizeof (sauce));
    text = decode (file->str, file->len);
    g_assert_false (text.binary); g_assert_false (text.damaged);
    g_assert_null (strstr (text.text, "SAUCE")); g_assert_null (strstr (text.text, "COMNT"));
    g_free (text.text); g_string_free (file, TRUE);
    const gchar *links = "日本語 (https://example.org/a_(b)). HTTPS://[::1]:8080/path; http://example.org/$(echo)?x=%20\n"
        "https:// https:///missing https://example.org/%XX file:///tmp/file javascript:alert(1)";
    GPtrArray *found = e2_viewer_find_links (links);
    g_assert_cmpuint (found->len, ==, 3);
    E2_ViewerLink *link = g_ptr_array_index (found, 0);
    g_assert_cmpstr (link->uri, ==, "https://example.org/a_(b)");
    g_assert_cmpint (link->start, ==, 5);
    g_assert_cmpstr (((E2_ViewerLink *)g_ptr_array_index (found, 1))->uri, ==, "HTTPS://[::1]:8080/path");
    g_assert_true (e2_viewer_valid_uri ("https://例え.jp/日本語"));
    g_assert_false (e2_viewer_valid_uri ("https://example.org:99999/"));
    g_assert_false (e2_viewer_valid_uri ("https://example.org/\\bad"));
    g_ptr_array_free (found, TRUE);
    GRand *random = g_rand_new_with_seed (427);
    for (guint i = 0; i < 300; i++)
    {
        guint8 bytes[200];
        guint length = g_rand_int_range (random, 0, sizeof (bytes));
        for (guint j = 0; j < length; j++) bytes[j] = g_rand_int_range (random, 0, 256);
        text = e2_viewer_decode (bytes, length, i % 2, i % 3 ? NULL : encodings[i % G_N_ELEMENTS (encodings)]);
        g_assert_true (g_utf8_validate (text.text, -1, NULL));
        g_free (text.text);
    }
    g_rand_free (random);
    g_print ("viewer text: Unicode, legacy artwork, SAUCE, malformed bytes, plain text and web links passed\n");
    return 0;
}
