/* GNU Source-highlight adapter for GTK 2. SPDX-License-Identifier: GPL-3.0-or-later */
#include "e2_viewer_syntax.h"
#include <srchilite/langmap.h>
#include <srchilite/langdefmanager.h>
#include <srchilite/regexrulefactory.h>
#include <srchilite/sourcehighlighter.h>
#include <srchilite/formattermanager.h>
#include <srchilite/settings.h>
#include <cstring>
#include <stdexcept>

namespace {
struct Style { const char *name, *light, *dark; };
const Style styles[] = {
    {"keyword", "#204a87", "#8cc4ff"},
    {"type", "#5c3566", "#d6a8e9"},
    {"classname", "#5c3566", "#d6a8e9"},
    {"function", "#75507b", "#d6a8e9"},
    {"string", "#4e6a06", "#b5d780"},
    {"regexp", "#4e6a06", "#b5d780"},
    {"specialchar", "#a40000", "#ff9f9f"},
    {"comment", "#626660", "#a6afa2"},
    {"number", "#8f5902", "#f0c674"},
    {"preproc", "#8f5902", "#f0c674"},
    {"symbol", "#75507b", "#d6a8e9"},
    {"variable", "#8f5902", "#f0c674"},
    {"cbracket", "#75507b", "#d6a8e9"},
    {"label", "#204a87", "#8cc4ff"},
    {"url", "#204a87", "#8cc4ff"},
    {"date", "#8f5902", "#f0c674"},
    {"time", "#8f5902", "#f0c674"}
};

struct Position {
    GtkTextBuffer *buffer;
    const char *text;
    size_t length, byte;
    const char *anchor;
    gint character;
};

class TagFormatter : public srchilite::Formatter {
    Position &position;
    GtkTextTag *tag;
public:
    TagFormatter (Position &p, GtkTextTag *t) : position(p), tag(t) {}
    void format (const std::string &s, const srchilite::FormatterParams *) override
    {
        size_t first = position.byte;
        position.byte += s.size ();
        if (position.byte > position.length ||
            std::memcmp (position.text + first, s.data (), s.size ()) != 0)
            throw std::runtime_error ("highlighted text differs from input");
        if (tag == NULL || s.empty ()) return;
        const char *start = position.text + first, *end = position.text + position.byte;
        /* GNU's regex engine counts bytes. Never apply a partial UTF-8 token,
         * and count characters from the last valid boundary, not from byte 0. */
        if ((*start & 0xc0) == 0x80 || (*end & 0xc0) == 0x80) return;
        position.character += g_utf8_strlen (position.anchor, start - position.anchor);
        gint offset = position.character;
        position.character += g_utf8_strlen (start, end - start);
        position.anchor = end;
        GtkTextIter a, b;
        gtk_text_buffer_get_iter_at_offset (position.buffer, &a, offset);
        gtk_text_buffer_get_iter_at_offset (position.buffer, &b, position.character);
        gtk_text_buffer_apply_tag (position.buffer, tag, &a, &b);
    }
};

std::string language_from_mime (srchilite::LangMap &map, const char *mime)
{
    const struct { const char *mime, *language; } mappings[] = {
        {"text/x-python", "python"}, {"text/x-python3", "python"}, {"text/x-shellscript", "sh"},
        {"application/x-shellscript", "sh"}, {"application/x-perl", "perl"},
        {"text/x-perl", "perl"}, {"application/x-ruby", "ruby"},
        {"text/x-ruby", "ruby"}, {"application/javascript", "js"},
        {"text/javascript", "js"}, {"application/json", "json"},
        {"application/xml", "xml"}, {"text/xml", "xml"},
        {"text/html", "html"}, {"text/x-php", "php"},
        {"application/x-php", "php"}, {"text/x-lua", "lua"},
        {"text/x-awk", "awk"}, {"text/x-tcl", "tcl"},
        {"text/x-makefile", "makefile"}, {"text/x-diff", "diff"}
    };
    for (const auto &entry : mappings)
        if (g_strcmp0 (mime, entry.mime) == 0) return map.getMappedFileName (entry.language);
    return "";
}
}

void e2_source_highlight_clear (GtkTextBuffer *buffer)
{
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds (buffer, &start, &end);
    for (const auto &style : styles)
    {
        std::string name = std::string ("viewer-syntax-") + style.name;
        GtkTextTag *tag = gtk_text_tag_table_lookup (gtk_text_buffer_get_tag_table (buffer), name.c_str ());
        if (tag != NULL) gtk_text_buffer_remove_tag (buffer, tag, &start, &end);
    }
}

gboolean e2_source_highlight (GtkTextBuffer *buffer, const gchar *filename,
    const gchar *text, const gchar *mime, const gchar *data_dir, gboolean dark)
{
    /* No C++ exception may cross the C viewer's boundary. Missing definitions
     * and regex failures leave the original text available without colours. */
    try {
        std::string data = data_dir != NULL ? data_dir : srchilite::Settings::retrieveDataDir ();
        srchilite::LangMap map (data, "lang.map");
        std::string language = map.getMappedFileNameFromFileName (filename);
        if (language.empty ()) language = language_from_mime (map, mime);
        if (language.empty () || language == "nohilite.lang") return TRUE;
        srchilite::RegexRuleFactory factory;
        srchilite::LangDefManager definitions (&factory);
        srchilite::SourceHighlighter highlighter (definitions.getHighlightState (data, language));
        Position position = {buffer, text, std::strlen (text), 0, text, 0};
        srchilite::FormatterManager formatters (srchilite::FormatterPtr (new TagFormatter (position, NULL)));
        for (const auto &style : styles)
        {
            std::string name = std::string ("viewer-syntax-") + style.name;
            GtkTextTag *tag = gtk_text_tag_table_lookup (gtk_text_buffer_get_tag_table (buffer), name.c_str ());
            if (tag == NULL)
            {
                tag = gtk_text_buffer_create_tag (buffer, name.c_str (), NULL);
                gtk_text_tag_set_priority (tag, 0);
            }
            g_object_set (tag, "foreground", dark ? style.dark : style.light, NULL);
            formatters.addFormatter (style.name, srchilite::FormatterPtr (new TagFormatter (position, tag)));
        }
        highlighter.setFormatterManager (&formatters);
        highlighter.setOptimize (false);
        const char *line = text;
        while (*line != '\0')
        {
            const char *newline = std::strchr (line, '\n');
            const char *end = newline != NULL ? newline + 1 : text + position.length;
            highlighter.highlightParagraph (std::string (line, end));
            line = end;
        }
        return position.byte == position.length;
    }
    catch (...) { return FALSE; }
}
