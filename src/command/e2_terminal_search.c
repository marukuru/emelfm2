/* Shared log/terminal search. SPDX-License-Identifier: GPL-3.0-or-later */
#include "emelfm2.h"
#include "e2_terminal_search.h"
#include "e2_terminal_backend.h"
#include "e2_icons.h"

typedef struct
{
    GtkWidget *bar, *entry, *result, *target;
} ToolsSearch;

static gboolean log_search (ToolsSearch *search, const gchar *text, gboolean backwards, gboolean reset)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (search->target));
    GtkTextIter start, end, from;
    if (!reset && gtk_text_buffer_get_selection_bounds (buffer, &start, &end))
        from = backwards ? start : end;
    else if (backwards) gtk_text_buffer_get_end_iter (buffer, &from);
    else gtk_text_buffer_get_start_iter (buffer, &from);
    GtkTextIter first, last;
    gtk_text_buffer_get_bounds (buffer, &first, &last);
    gchar *contents = gtk_text_buffer_get_text (buffer, &first, &last, TRUE);
    gint anchor = g_utf8_offset_to_pointer (contents, gtk_text_iter_get_offset (&from)) - contents;
    gchar *pattern = g_regex_escape_string (text, -1);
    GRegex *regex = g_regex_new (pattern, G_REGEX_CASELESS, 0, NULL);
    GMatchInfo *matches;
    g_regex_match (regex, contents, 0, &matches);
    gint chosen_start = -1, chosen_end = -1, wrap_start = -1, wrap_end = -1;
    while (g_match_info_matches (matches))
    {
        gint begin, finish;
        g_match_info_fetch_pos (matches, 0, &begin, &finish);
        if (wrap_start < 0 || backwards) { wrap_start = begin; wrap_end = finish; }
        if ((!backwards && begin >= anchor) || (backwards && finish <= anchor))
        {
            chosen_start = begin; chosen_end = finish;
            if (!backwards) break;
        }
        g_match_info_next (matches, NULL);
    }
    if (chosen_start < 0) { chosen_start = wrap_start; chosen_end = wrap_end; }
    gboolean found = chosen_start >= 0;
    if (found)
    {
        gtk_text_buffer_get_iter_at_offset (buffer, &start, g_utf8_pointer_to_offset (contents, contents + chosen_start));
        gtk_text_buffer_get_iter_at_offset (buffer, &end, g_utf8_pointer_to_offset (contents, contents + chosen_end));
    }
    g_match_info_free (matches);
    g_regex_unref (regex);
    g_free (pattern); g_free (contents);
    if (found)
    {
        gtk_text_buffer_select_range (buffer, &start, &end);
        gtk_text_view_scroll_to_iter (GTK_TEXT_VIEW (search->target), &start, 0.1, FALSE, 0, 0);
    }
    return found;
}
static void find_text (ToolsSearch *search, gboolean backwards, gboolean reset)
{
    const gchar *text = gtk_entry_get_text (GTK_ENTRY (search->entry));
    gboolean found = FALSE;
    if (search->target != NULL)
    {
        if (GTK_IS_TEXT_VIEW (search->target))
        {
            if (*text) found = log_search (search, text, backwards, reset);
        }
        else found = e2_terminal_backend_search (search->target, text, backwards);
    }
    gtk_label_set_text (GTK_LABEL (search->result), *text && !found ? _("No matches") : "");
}
static void changed (GtkEditable *entry, ToolsSearch *search) { find_text (search, FALSE, TRUE); }
static void next (GtkWidget *button, ToolsSearch *search) { find_text (search, FALSE, FALSE); }
static void previous (GtkWidget *button, ToolsSearch *search) { find_text (search, TRUE, FALSE); }
static void close_search (GtkWidget *button, ToolsSearch *search)
{
    gtk_widget_hide (search->bar);
    if (search->target != NULL) gtk_widget_grab_focus (search->target);
}
static gboolean key (GtkWidget *entry, GdkEventKey *event, ToolsSearch *search)
{
    if (event->keyval == GDK_Escape) { close_search (NULL, search); return TRUE; }
    if (event->keyval == GDK_Return || event->keyval == GDK_KP_Enter)
    {
        find_text (search, (event->state & GDK_SHIFT_MASK) != 0, FALSE);
        return TRUE;
    }
    return FALSE;
}
static void destroy_search (GtkWidget *bar, ToolsSearch *search)
{
    if (search->target != NULL)
        g_object_remove_weak_pointer (G_OBJECT (search->target), (gpointer *) &search->target);
    search->target = NULL;
}
GtkWidget *e2_terminal_search_new (void)
{
    ToolsSearch *search = g_new0 (ToolsSearch, 1);
    search->bar = gtk_hbox_new (FALSE, 3);
    gtk_widget_set_name (search->bar, "tools-search");
    search->entry = gtk_entry_new ();
    gtk_entry_set_width_chars (GTK_ENTRY (search->entry), 8);
    gtk_widget_set_name (search->entry, "tools-search-entry");
    gtk_widget_set_tooltip_text (search->entry, _("Find text, ignoring case. Enter: next; Shift+Enter: previous; Escape: close."));
    atk_object_set_name (gtk_widget_get_accessible (search->entry), _("Find in selected view"));
    gtk_box_pack_start (GTK_BOX (search->bar), search->entry, TRUE, TRUE, 0);
    search->result = gtk_label_new ("");
    gtk_label_set_ellipsize (GTK_LABEL (search->result), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars (GTK_LABEL (search->result), 10);
    gtk_widget_set_name (search->result, "tools-search-result");
    gtk_box_pack_start (GTK_BOX (search->bar), search->result, FALSE, FALSE, 0);
    GtkWidget *button = e2_button_add (search->bar, FALSE, 0, NULL, STOCK_NAME_GO_UP,
        _("Previous match"), previous, search);
    atk_object_set_name (gtk_widget_get_accessible (button), _("Previous match"));
    button = e2_button_add (search->bar, FALSE, 0, NULL, STOCK_NAME_GO_DOWN,
        _("Next match"), next, search);
    atk_object_set_name (gtk_widget_get_accessible (button), _("Next match"));
    button = e2_button_add (search->bar, FALSE, 0, NULL, STOCK_NAME_CLOSE,
        _("Close search"), close_search, search);
    atk_object_set_name (gtk_widget_get_accessible (button), _("Close search"));
    g_signal_connect (search->entry, "changed", G_CALLBACK (changed), search);
    g_signal_connect (search->entry, "activate", G_CALLBACK (next), search);
    g_signal_connect (search->entry, "key-press-event", G_CALLBACK (key), search);
    g_signal_connect (search->bar, "destroy", G_CALLBACK (destroy_search), search);
    g_object_set_data_full (G_OBJECT (search->bar), "tools-search", search, g_free);
    gtk_widget_show_all (search->bar);
    gtk_widget_set_no_show_all (search->bar, TRUE);
    gtk_widget_hide (search->bar);
    return search->bar;
}
void e2_terminal_search_target (GtkWidget *bar, GtkWidget *target)
{
    ToolsSearch *search = g_object_get_data (G_OBJECT (bar), "tools-search");
    if (search->target == target) return;
    if (search->target != NULL)
        g_object_remove_weak_pointer (G_OBJECT (search->target), (gpointer *) &search->target);
    search->target = target;
    if (target != NULL) g_object_add_weak_pointer (G_OBJECT (target), (gpointer *) &search->target);
    if (gtk_widget_get_visible (bar)) find_text (search, FALSE, TRUE);
}
void e2_terminal_search_show (GtkWidget *bar, GtkWidget *target)
{
    e2_terminal_search_target (bar, target);
    ToolsSearch *search = g_object_get_data (G_OBJECT (bar), "tools-search");
    gtk_widget_show (bar);
    gtk_widget_grab_focus (search->entry);
    gtk_editable_select_region (GTK_EDITABLE (search->entry), 0, -1);
}
