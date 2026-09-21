/* Internal viewer artwork and encoding detection.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "e2_viewer_text.h"
#include <gio/gio.h>
#include <string.h>

static E2_ViewerArt byte_art (const guint8 *bytes, gsize length)
{
    guint blocks = 0, pairs = 0, run = 0, topaz = 0;
    guint horizontal = 0, horizontal_run = 0, vertical = 0, corners = 0;
    for (gsize i = 0; i < length; i++)
    {
        guint8 c = bytes[i];
        if ((c >= 0xb0 && c <= 0xb2) || (c >= 0xdb && c <= 0xdf))
        { blocks++; if (++run % 2 == 0) pairs++; }
        else run = 0;
        if (c == 0xaf || c == 0xa6 || c == 0xac || c == 0xd8 || c == 0xc6) topaz++;
        if (c == 0xc4 || c == 0xcd) { if (++horizontal_run == 2) horizontal++; }
        else horizontal_run = 0;
        if (c == 0xb3 || c == 0xba) vertical++;
        if (c == 0xda || c == 0xbf || c == 0xc0 || c == 0xd9 || c == 0xc9
            || c == 0xbb || c == 0xc8 || c == 0xbc) corners++;
    }
    if ((blocks >= 6 && pairs >= 2 && blocks > topaz * 2)
        || (horizontal > 0 && corners >= 2 && blocks + vertical + corners > topaz * 2))
        return E2_VIEWER_PC;
    return topaz >= 3 ? E2_VIEWER_AMIGA : E2_VIEWER_PLAIN;
}

static E2_ViewerArt unicode_art (const gchar *text)
{
    guint graphic = 0, visible = 0, art_lines = 0, topaz = 0;
    for (const gchar *p = text; ; p = g_utf8_next_char (p))
    {
        gunichar c = g_utf8_get_char (p);
        if (c >= 0x2500 && c <= 0x259f) return E2_VIEWER_PC;
        if (c == 0xaf || c == 0xa6 || c == 0xac || c == 0xd8 || c == 0xc6) topaz++;
        if (c == '\n' || c == 0)
        {
            if (graphic >= 6 && graphic * 2 >= visible) art_lines++;
            if (graphic >= 12 && graphic * 4 >= visible * 3) return E2_VIEWER_AMIGA;
            graphic = visible = 0;
            if (c == 0) break;
        }
        else if (!g_unichar_isspace (c))
        {
            visible++;
            if (c < 128 && strchr ("/\\|_-+=.:`~()[]{}<>^", c) != NULL) graphic++;
        }
    }
    return topaz >= 3 || art_lines >= 2 ? E2_VIEWER_AMIGA : E2_VIEWER_PLAIN;
}

static const gchar *detect_encoding (const guint8 *bytes, gsize length, E2_ViewerArt art)
{
    if (length >= 4 && !memcmp (bytes, "\0\0\xfe\xff", 4)) return "UTF-32BE";
    if (length >= 4 && !memcmp (bytes, "\xff\xfe\0\0", 4)) return "UTF-32LE";
    if (length >= 2 && !memcmp (bytes, "\xff\xfe", 2)) return "UTF-16LE";
    if (length >= 2 && !memcmp (bytes, "\xfe\xff", 2)) return "UTF-16BE";
    if (length >= 3 && !memcmp (bytes, "\xef\xbb\xbf", 3)) return "UTF-8";
    guint sample = MIN (length, 4096), zero[4] = {0};
    for (guint i = 0; i < sample; i++) if (bytes[i] == 0) zero[i % 4]++;
    if (sample >= 8 && (zero[1]+zero[2]+zero[3])*100 > sample*65 && zero[0]*100 < sample*5) return "UTF-32LE";
    if (sample >= 8 && (zero[0]+zero[1]+zero[2])*100 > sample*65 && zero[3]*100 < sample*5) return "UTF-32BE";
    if (sample >= 4 && (zero[1]+zero[3])*100 > sample*35 && (zero[0]+zero[2])*100 < sample*5) return "UTF-16LE";
    if (sample >= 4 && (zero[0]+zero[2])*100 > sample*35 && (zero[1]+zero[3])*100 < sample*5) return "UTF-16BE";
    if (g_utf8_validate ((const gchar *)bytes, length, NULL)) return "UTF-8";
    return art == E2_VIEWER_PC ? "CP437" : art == E2_VIEWER_AMIGA ? "ISO-8859-1" : "WINDOWS-1252";
}

E2_ViewerText e2_viewer_decode (const guint8 *bytes, gsize length, gboolean detect_art,
    const gchar *encoding)
{
    E2_ViewerText result = {0};
    if (bytes == NULL) { bytes = (const guint8 *)""; length = 0; }
    /* SAUCE describes the artwork but is never evidence for its encoding. */
    if (detect_art && length >= 128 && !memcmp (bytes + length - 128, "SAUCE00", 7))
    {
        guint comments = bytes[length - 128 + 104];
        length -= 128;
        if (comments && length >= 5 + comments * 64
            && !memcmp (bytes + length - 5 - comments * 64, "COMNT", 5)) length -= 5 + comments * 64;
        if (length && bytes[length - 1] == 0x1a) length--;
    }
    E2_ViewerArt hint = detect_art ? byte_art (bytes, length) : E2_VIEWER_PLAIN;
    result.encoding = encoding != NULL && *encoding ? encoding : detect_encoding (bytes, length, hint);
    GString *converted = g_string_new (NULL);
    gsize at = 0;
    while (at < length)
    {
        gsize read = 0, written = 0;
        GError *error = NULL;
        gchar *part = g_convert ((const gchar *)bytes + at, length - at, "UTF-8", result.encoding,
            &read, &written, &error);
        if (part != NULL)
        {
            g_string_append_len (converted, part, written);
            g_free (part);
            at += read;
            if (at == length) break;
        }
        else if (read > 0)
        {
            part = g_convert ((const gchar *)bytes + at, read, "UTF-8", result.encoding, NULL, &written, NULL);
            if (part != NULL) { g_string_append_len (converted, part, written); g_free (part); }
            at += read;
        }
        g_clear_error (&error);
        result.damaged = TRUE;
        g_string_append_unichar (converted, 0xfffd);
        guint unit = g_str_has_prefix (result.encoding, "UTF-32") ? 4 : g_str_has_prefix (result.encoding, "UTF-16") ? 2 : 1;
        at += MIN (length - at, unit);
    }
    GString *display = g_string_new (NULL);
    const gchar *p = converted->str, *end = p + converted->len;
    if (converted->len >= 3 && !memcmp (p, "\xef\xbb\xbf", 3)) p += 3;
    while (p < end)
    {
        gunichar c = g_utf8_get_char (p);
        p = g_utf8_next_char (p);
        if (c == '\r') { if (p < end && *p == '\n') p++; c = '\n'; }
        if (c == 0) { result.binary = TRUE; c = 0x2400; }
        g_string_append_unichar (display, c);
    }
    g_string_free (converted, TRUE);
    result.text = g_string_free (display, FALSE);
    if (detect_art)
    {
        result.art = unicode_art (result.text);
        if (!strcmp (result.encoding, "CP437")) result.art = E2_VIEWER_PC;
    }
    return result;
}

/* Restrict links to web URLs. Browser executables and URLs are separate argv
 * entries; no document text is ever interpreted as a command or shell code. */
gboolean e2_viewer_valid_uri (const gchar *uri)
{
    if (g_ascii_strncasecmp (uri, "http://", 7) && g_ascii_strncasecmp (uri, "https://", 8)) return FALSE;
    for (const gchar *p = uri; *p; p++)
    {
        if ((guchar)*p <= 0x20 || *p == 0x7f || strchr ("\\<>\"", *p)) return FALSE;
        if (*p == '%' && (!g_ascii_isxdigit (p[1]) || !p[1] || !g_ascii_isxdigit (p[2]))) return FALSE;
    }
    const gchar *start = strstr (uri, "://") + 3;
    gchar *authority = g_strndup (start, strcspn (start, "/?#"));
    gchar *host = strrchr (authority, '@'); host = host == NULL ? authority : host + 1;
    gchar *port = NULL;
    gboolean valid = FALSE;
    if (*host == '[')
    {
        gchar *close = strchr (++host, ']');
        if (close != NULL)
        {
            *close = 0;
            GInetAddress *address = g_inet_address_new_from_string (host);
            valid = address != NULL && g_inet_address_get_family (address) == G_SOCKET_FAMILY_IPV6;
            if (address != NULL) g_object_unref (address);
            if (close[1] == ':') port = close + 2;
            else if (close[1]) valid = FALSE;
        }
    }
    else
    {
        port = strchr (host, ':');
        if (port != NULL) *port++ = 0;
        gchar *ascii = g_hostname_to_ascii (host);
        valid = ascii != NULL && *ascii != 0;
        if (ascii != NULL) for (gchar *p = ascii; *p; p++)
            if (!g_ascii_isalnum (*p) && *p != '-' && *p != '.' && *p != '_') valid = FALSE;
        g_free (ascii);
    }
    if (port != NULL)
    {
        if (!*port || strlen (port) > 5) valid = FALSE;
        for (gchar *p = port; *p; p++) if (!g_ascii_isdigit (*p)) valid = FALSE;
        if (g_ascii_strtoull (port, NULL, 10) > 65535) valid = FALSE;
    }
    g_free (authority);
    return valid;
}
void e2_viewer_link_free (gpointer data)
{
    E2_ViewerLink *link = data;
    g_free (link->uri); g_free (link);
}
GPtrArray *e2_viewer_find_links (const gchar *text)
{
    GPtrArray *links = g_ptr_array_new_with_free_func (e2_viewer_link_free);
    GRegex *regex = g_regex_new ("\\bhttps?://[^\\s<>\"'\\x00-\\x1f\\x7f]+", G_REGEX_CASELESS, 0, NULL);
    GMatchInfo *match;
    g_regex_match (regex, text, 0, &match);
    gint byte_offset = 0, char_offset = 0;
    while (g_match_info_matches (match))
    {
        gint start, end;
        g_match_info_fetch_pos (match, 0, &start, &end);
        char_offset += g_utf8_pointer_to_offset (text + byte_offset, text + start);
        byte_offset = start;
        gchar *uri = g_strndup (text + start, end - start);
        gint round = 0, square = 0, curly = 0;
        for (gchar *p = uri; *p; p++)
        { round += (*p == '(') - (*p == ')'); square += (*p == '[') - (*p == ']'); curly += (*p == '{') - (*p == '}'); }
        gsize n = strlen (uri);
        while (n)
        {
            gchar c = uri[n-1];
            if (strchr (".,;", c) != NULL) uri[--n] = 0;
            else if (c == ')' && round < 0) { uri[--n] = 0; round++; }
            else if (c == ']' && square < 0) { uri[--n] = 0; square++; }
            else if (c == '}' && curly < 0) { uri[--n] = 0; curly++; }
            else break;
        }
        if (e2_viewer_valid_uri (uri))
        {
            E2_ViewerLink *link = g_new (E2_ViewerLink, 1);
            link->start = char_offset;
            link->end = link->start + g_utf8_strlen (uri, -1);
            link->uri = uri;
            g_ptr_array_add (links, link);
        }
        else g_free (uri);
        g_match_info_next (match, NULL);
    }
    g_match_info_free (match); g_regex_unref (regex);
    return links;
}
