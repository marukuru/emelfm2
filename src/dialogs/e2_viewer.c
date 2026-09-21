/* Internal file viewer. SPDX-License-Identifier: GPL-3.0-or-later */
#include "emelfm2.h"
#include "e2_viewer.h"
#include "e2_viewer_text.h"
#include "e2_option.h"
#include <fontconfig/fontconfig.h>
#include <pango/pangofc-fontmap.h>
#include <pango/pangocairo.h>
#include <gio/gio.h>
#include <string.h>

/* Keep the encoding group and the dialog's action area on one line when they
 * fit. Retaining the action area preserves GtkDialog responses and mnemonics.
 * GTK2 needs a second size request when wrapping changes; GTK3 can negotiate
 * the required height directly from the available width. */
typedef struct
{
    GtkContainer parent;
    GtkWidget *encoding, *info, *actions;
    gboolean wrapped;
} E2ViewerControls;
typedef struct { GtkContainerClass parent; } E2ViewerControlsClass;
G_DEFINE_TYPE (E2ViewerControls, e2_viewer_controls, GTK_TYPE_CONTAINER)
#define VIEWER_CONTROL_SPACING 6
static void controls_add (GtkContainer *container, GtkWidget *child)
{
    E2ViewerControls *controls = (E2ViewerControls *)container;
    if (controls->encoding == NULL) controls->encoding = child;
    else { g_return_if_fail (controls->actions == NULL); controls->actions = child; }
    gtk_widget_set_parent (child, GTK_WIDGET (container));
    gtk_widget_queue_resize (GTK_WIDGET (container));
}
static void controls_remove (GtkContainer *container, GtkWidget *child)
{
    E2ViewerControls *controls = (E2ViewerControls *)container;
    if (child == controls->encoding) { controls->encoding = NULL; controls->info = NULL; }
    else if (child == controls->actions) controls->actions = NULL;
    else return;
    gtk_widget_unparent (child);
    gtk_widget_queue_resize (GTK_WIDGET (container));
}
static void controls_forall (GtkContainer *container, gboolean internal, GtkCallback callback, gpointer data)
{
    E2ViewerControls *controls = (E2ViewerControls *)container;
    if (controls->encoding != NULL) callback (controls->encoding, data);
    if (controls->actions != NULL) callback (controls->actions, data);
}
static void controls_measure (E2ViewerControls *controls, GtkRequisition *encoding,
    GtkRequisition *actions, gint *single_width)
{
    *encoding = (GtkRequisition){0, 0};
    *actions = (GtkRequisition){0, 0};
    if (controls->encoding != NULL) gtk_widget_size_request (controls->encoding, encoding);
    if (controls->actions != NULL) gtk_widget_size_request (controls->actions, actions);
    gint info_width = 0;
    if (controls->info != NULL)
    {
        GtkRequisition info;
        gtk_widget_size_request (controls->info, &info);
        PangoLayout *layout = gtk_widget_create_pango_layout (controls->info,
            gtk_label_get_text (GTK_LABEL (controls->info)));
        pango_layout_get_pixel_size (layout, &info_width, NULL);
        g_object_unref (layout);
        info_width = MAX (0, info_width - info.width);
    }
    *single_width = encoding->width + info_width + VIEWER_CONTROL_SPACING + actions->width;
}
#ifdef USE_GTK3_0
static GtkSizeRequestMode controls_request_mode (GtkWidget *widget)
{ return GTK_SIZE_REQUEST_HEIGHT_FOR_WIDTH; }
static void controls_width (GtkWidget *widget, gint *minimum, gint *natural)
{
    GtkRequisition encoding, actions;
    controls_measure ((E2ViewerControls *)widget, &encoding, &actions, natural);
    *minimum = MAX (encoding.width, actions.width);
}
static void controls_height_for_width (GtkWidget *widget, gint width, gint *minimum, gint *natural)
{
    GtkRequisition encoding, actions;
    gint single_width;
    controls_measure ((E2ViewerControls *)widget, &encoding, &actions, &single_width);
    *minimum = *natural = width < single_width
        ? encoding.height + VIEWER_CONTROL_SPACING + actions.height : MAX (encoding.height, actions.height);
}
static void controls_height (GtkWidget *widget, gint *minimum, gint *natural)
{
    gint width, unused;
    controls_width (widget, &width, &unused);
    controls_height_for_width (widget, width, minimum, natural);
}
#else
static void controls_request (GtkWidget *widget, GtkRequisition *request)
{
    E2ViewerControls *controls = (E2ViewerControls *)widget;
    GtkRequisition encoding, actions;
    gint single_width;
    controls_measure (controls, &encoding, &actions, &single_width);
    request->width = MAX (encoding.width, actions.width);
    request->height = controls->wrapped ? encoding.height + VIEWER_CONTROL_SPACING + actions.height
        : MAX (encoding.height, actions.height);
}
#endif
static void controls_allocate (GtkWidget *widget, GtkAllocation *allocation)
{
    E2ViewerControls *controls = (E2ViewerControls *)widget;
    GtkRequisition encoding, actions;
    gint single_width;
    controls_measure (controls, &encoding, &actions, &single_width);
    gboolean wrapped = allocation->width < single_width;
    gtk_widget_set_allocation (widget, allocation);
#ifndef USE_GTK3_0
    if (wrapped != controls->wrapped) gtk_widget_queue_resize (widget);
#endif
    controls->wrapped = wrapped;
    gboolean rtl = gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL;
    gint first_width = wrapped ? allocation->width : allocation->width - actions.width - VIEWER_CONTROL_SPACING;
    GtkAllocation first = {allocation->x + (rtl && !wrapped ? actions.width + VIEWER_CONTROL_SPACING : 0),
        allocation->y + (wrapped ? 0 : (allocation->height - encoding.height) / 2),
        MAX (1, first_width), encoding.height};
    GtkAllocation second = {allocation->x + (rtl || wrapped ? 0 : first_width + VIEWER_CONTROL_SPACING),
        allocation->y + (wrapped ? encoding.height + VIEWER_CONTROL_SPACING : (allocation->height - actions.height) / 2),
        wrapped ? allocation->width : actions.width, actions.height};
    if (controls->encoding != NULL) gtk_widget_size_allocate (controls->encoding, &first);
    if (controls->actions != NULL) gtk_widget_size_allocate (controls->actions, &second);
}
static void e2_viewer_controls_class_init (E2ViewerControlsClass *klass)
{
    GtkContainerClass *container = GTK_CONTAINER_CLASS (klass);
    container->add = controls_add;
    container->remove = controls_remove;
    container->forall = controls_forall;
    GtkWidgetClass *widget = GTK_WIDGET_CLASS (klass);
    widget->size_allocate = controls_allocate;
#ifdef USE_GTK3_0
    widget->get_request_mode = controls_request_mode;
    widget->get_preferred_width = controls_width;
    widget->get_preferred_height = controls_height;
    widget->get_preferred_height_for_width = controls_height_for_width;
#else
    widget->size_request = controls_request;
#endif
}
static void e2_viewer_controls_init (E2ViewerControls *controls)
{
    gtk_widget_set_has_window (GTK_WIDGET (controls), FALSE);
    controls->wrapped = TRUE;
}

struct _E2_Viewer
{
    gpointer bytes;
    gsize length;
    E2_ViewerText decoded;
    GtkTextBuffer *buffer;
    GtkWidget *view, *scroll, *info;
    E2ViewerControls *controls;
    GPtrArray *links;
    gint char_width, char_height, gutter, press_x, press_y;
    gchar *pressed;
    gboolean dragged;
    PangoFontDescription *font;
};
static const gchar *encodings[] = {NULL, "UTF-8", "UTF-16LE", "UTF-16BE",
    "UTF-32LE", "UTF-32BE", "CP437", "ISO-8859-1", "WINDOWS-1252"};

static void register_fonts (void)
{
    static gboolean done;
    if (done) return;
    done = TRUE;
    /* Executable-relative assets support relocatable AppImages. The source
     * directory fallback also makes an uninstalled development build usable. */
    gchar *executable = g_file_read_link ("/proc/self/exe", NULL);
    gchar *bin = executable == NULL ? NULL : g_path_get_dirname (executable);
    gchar *relative = bin == NULL ? NULL : g_build_filename (bin, "..", "share", BINNAME, "fonts", NULL);
    const gchar *directories[] = {relative, PREFIX "/share/" BINNAME "/fonts",
        E2_VIEWER_FONT_SOURCE_DIR};
    const gchar *names[] = {"IBM_VGA_8x16.ttf", "Topaz_a1200.ttf"};
    for (guint i = 0; i < G_N_ELEMENTS (names); i++)
        for (guint j = 0; j < G_N_ELEMENTS (directories); j++)
        {
            if (directories[j] == NULL) continue;
            gchar *path = g_build_filename (directories[j], names[i], NULL);
            gboolean loaded = FcConfigAppFontAddFile (FcConfigGetCurrent (), (const FcChar8 *)path);
            g_free (path);
            if (loaded) break;
        }
    PangoFontMap *map = pango_cairo_font_map_get_default ();
    if (PANGO_IS_FC_FONT_MAP (map)) pango_fc_font_map_config_changed (PANGO_FC_FONT_MAP (map));
    g_free (relative); g_free (bin); g_free (executable);
}
static void update_gutter (E2_Viewer *viewer)
{
    gint lines = gtk_text_buffer_get_line_count (viewer->buffer), digits = 1;
    while (lines >= 10) { lines /= 10; digits++; }
    viewer->gutter = e2_option_bool_get ("dialog-view-line-numbers") ? digits * viewer->char_width + 12 : 0;
    gtk_text_view_set_border_window_size (GTK_TEXT_VIEW (viewer->view), GTK_TEXT_WINDOW_LEFT, viewer->gutter);
}
void e2_viewer_set_font (E2_Viewer *viewer, GtkWidget *view, gint *width, gint *height)
{
    register_fonts ();
    if (viewer->font != NULL) pango_font_description_free (viewer->font);
    gchar *theme_font = NULL;
    const gchar *name;
    if (e2_option_bool_get ("dialog-view-use-font")) name = e2_option_str_get ("dialog-view-font");
    else
    {
        g_object_get (gtk_settings_get_default (), "gtk-font-name", &theme_font, NULL);
        name = theme_font;
    }
    if (viewer->decoded.art == E2_VIEWER_PC) name = "PxPlus IBM VGA 8x16";
    else if (viewer->decoded.art == E2_VIEWER_AMIGA) name = "Topaz a600a1200a400";
    if (name == NULL || !*name) name = "Monospace 10";
    viewer->font = pango_font_description_from_string (name);
    if (viewer->decoded.art != E2_VIEWER_PLAIN)
    {
        /* Family names ending in digits must not be parsed as font sizes. */
        pango_font_description_set_family (viewer->font, name);
        pango_font_description_set_absolute_size (viewer->font, 16 * PANGO_SCALE);
    }
    g_free (theme_font);
#ifdef USE_GTK3_0
    gtk_widget_override_font (view, viewer->font);
#else
    gtk_widget_modify_font (view, viewer->font);
#endif
    PangoContext *context = gtk_widget_get_pango_context (view);
    cairo_font_options_t *options = cairo_font_options_create ();
    if (viewer->decoded.art != E2_VIEWER_PLAIN)
    {
        cairo_font_options_set_antialias (options, CAIRO_ANTIALIAS_NONE);
        cairo_font_options_set_hint_style (options, CAIRO_HINT_STYLE_NONE);
    }
    pango_cairo_context_set_font_options (context, options);
    cairo_font_options_destroy (options);
    PangoLayout *layout = gtk_widget_create_pango_layout (view, "M");
    pango_layout_set_font_description (layout, viewer->font);
    pango_layout_get_pixel_size (layout, &viewer->char_width, &viewer->char_height);
    g_object_unref (layout);
    viewer->char_width = MAX (1, viewer->char_width);
    viewer->char_height = MAX (1, viewer->char_height);
    PangoTabArray *tabs = pango_tab_array_new (1, TRUE);
    pango_tab_array_set_tab (tabs, 0, PANGO_TAB_LEFT, viewer->char_width * 8);
    gtk_text_view_set_tabs (GTK_TEXT_VIEW (view), tabs);
    pango_tab_array_free (tabs);
    *width = viewer->char_width; *height = viewer->char_height;
    if (viewer->view != NULL) update_gutter (viewer);
}
static void update_links (E2_Viewer *viewer)
{
    GtkTextTagTable *table = gtk_text_buffer_get_tag_table (viewer->buffer);
    GtkTextTag *tag = gtk_text_tag_table_lookup (table, "viewer-link");
    if (tag == NULL) tag = gtk_text_buffer_create_tag (viewer->buffer, "viewer-link",
        "underline", PANGO_UNDERLINE_SINGLE, NULL);
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds (viewer->buffer, &start, &end);
    gtk_text_buffer_remove_tag (viewer->buffer, tag, &start, &end);
    g_object_set (tag, "foreground", e2_option_str_get ("dialog-view-link-color"), NULL);
    if (viewer->links != NULL) g_ptr_array_free (viewer->links, TRUE);
    viewer->links = e2_option_bool_get ("dialog-view-links") ? e2_viewer_find_links (viewer->decoded.text) : NULL;
    if (viewer->links != NULL) for (guint i = 0; i < viewer->links->len; i++)
    {
        E2_ViewerLink *link = g_ptr_array_index (viewer->links, i);
        gtk_text_buffer_get_iter_at_offset (viewer->buffer, &start, link->start);
        gtk_text_buffer_get_iter_at_offset (viewer->buffer, &end, link->end);
        gtk_text_buffer_apply_tag (viewer->buffer, tag, &start, &end);
    }
}
static void update_info (E2_Viewer *viewer)
{
    const gchar *name = viewer->decoded.art == E2_VIEWER_PC ? "IBM VGA 8×16" :
        viewer->decoded.art == E2_VIEWER_AMIGA ? "Amiga Topaz" : _("Text");
    GString *text = g_string_new (viewer->decoded.encoding);
    g_string_append_printf (text, " · %s", name);
    if (viewer->decoded.damaged)
        g_string_append_printf (text, " · %s", _("Invalid bytes replaced; try another encoding"));
    if (viewer->decoded.binary)
    {
        g_string_append (text, " · ");
        g_string_append_printf (text, _("NUL bytes shown as %s"), "␀");
    }
    gtk_label_set_text (GTK_LABEL (viewer->info), text->str);
    gtk_widget_set_tooltip_text (viewer->info, text->str);
    g_string_free (text, TRUE);
}
static void encoding_changed (GtkComboBox *combo, E2_Viewer *viewer)
{
    gint selected = gtk_combo_box_get_active (combo);
    if (selected < 0 || selected >= G_N_ELEMENTS (encodings)) return;
    NEEDCLOSEBGL
    g_clear_pointer (&viewer->pressed, g_free);
    g_free (viewer->decoded.text);
    viewer->decoded = e2_viewer_decode (viewer->bytes, viewer->length,
        e2_option_bool_get ("dialog-view-ascii-art"), encodings[selected]);
    gtk_text_buffer_set_text (viewer->buffer, viewer->decoded.text, -1);
    update_links (viewer);
    e2_viewer_set_font (viewer, viewer->view, &viewer->char_width, &viewer->char_height);
    if (viewer->decoded.art != E2_VIEWER_PLAIN)
    {
        GtkWidget *wrap = g_object_get_data (G_OBJECT (viewer->view), "viewer-wrap-toggle");
        if (wrap != NULL) gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (wrap), FALSE);
        gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW (viewer->view), GTK_WRAP_NONE);
    }
    update_info (viewer);
    NEEDOPENBGL
}
static E2_ViewerLink *link_at (E2_Viewer *viewer, GdkWindow *window, gint x, gint y)
{
    GtkTextView *view = GTK_TEXT_VIEW (viewer->view);
    if (viewer->links == NULL || !e2_option_bool_get ("dialog-view-links")
        || window != gtk_text_view_get_window (view, GTK_TEXT_WINDOW_TEXT)) return NULL;
    gtk_text_view_window_to_buffer_coords (view, GTK_TEXT_WINDOW_TEXT, x, y, &x, &y);
    GtkTextIter iter;
    gtk_text_view_get_iter_at_location (view, &iter, x, y);
    GdkRectangle rect;
    gtk_text_view_get_iter_location (view, &iter, &rect);
    if (x < MIN (rect.x, rect.x + rect.width) || x >= MAX (rect.x, rect.x + rect.width)
        || y < rect.y || y >= rect.y + rect.height) return NULL;
    gint offset = gtk_text_iter_get_offset (&iter), low = 0, high = viewer->links->len;
    while (low < high)
    {
        gint middle = low + (high - low) / 2;
        E2_ViewerLink *link = g_ptr_array_index (viewer->links, middle);
        if (offset < link->start) high = middle;
        else if (offset >= link->end) low = middle + 1;
        else return link;
    }
    return NULL;
}
static gboolean link_press (GtkWidget *widget, GdkEventButton *event, E2_Viewer *viewer)
{
    NEEDCLOSEBGL
    g_clear_pointer (&viewer->pressed, g_free);
    viewer->press_x = event->x; viewer->press_y = event->y; viewer->dragged = FALSE;
    if (event->type == GDK_BUTTON_PRESS && event->button == 1
        && !(event->state & gtk_accelerator_get_default_mod_mask ()))
    {
        E2_ViewerLink *link = link_at (viewer, event->window, event->x, event->y);
        if (link != NULL) viewer->pressed = g_strdup (link->uri);
    }
    NEEDOPENBGL
    return FALSE;
}
static gboolean link_motion (GtkWidget *widget, GdkEventMotion *event, E2_Viewer *viewer)
{
    NEEDCLOSEBGL
    if (viewer->pressed != NULL && gtk_drag_check_threshold (widget, viewer->press_x,
        viewer->press_y, event->x, event->y)) viewer->dragged = TRUE;
    E2_ViewerLink *link = link_at (viewer, event->window, event->x, event->y);
    GdkCursor *cursor = gdk_cursor_new_for_display (gtk_widget_get_display (widget),
        link != NULL && !(event->state & GDK_BUTTON1_MASK) ? GDK_HAND2 : GDK_XTERM);
    gdk_window_set_cursor (gtk_text_view_get_window (GTK_TEXT_VIEW (widget), GTK_TEXT_WINDOW_TEXT), cursor);
#ifdef USE_GTK3_0
    g_object_unref (cursor);
#else
    gdk_cursor_unref (cursor);
#endif
    NEEDOPENBGL
    return FALSE;
}
static gboolean link_release (GtkWidget *widget, GdkEventButton *event, E2_Viewer *viewer)
{
    NEEDCLOSEBGL
    E2_ViewerLink *link = link_at (viewer, event->window, event->x, event->y);
    gboolean clicked = event->button == 1 && viewer->pressed != NULL && link != NULL
        && !strcmp (viewer->pressed, link->uri) && !viewer->dragged
        && !(event->state & gtk_accelerator_get_default_mod_mask ())
        && !gtk_drag_check_threshold (widget, viewer->press_x, viewer->press_y, event->x, event->y)
        && !gtk_text_buffer_get_has_selection (viewer->buffer);
    if (clicked)
    {
        gchar *uri = g_uri_escape_string (link->uri, ":/?#[]@!$&'()*+,;=%", FALSE);
        GError *error = NULL;
        gboolean opened;
        if (e2_option_bool_get ("dialog-view-custom-browser"))
        {
            gchar *argv[] = {e2_option_str_get ("dialog-view-browser"), uri, NULL};
            opened = g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error);
        }
        else opened = g_app_info_launch_default_for_uri (uri, NULL, &error);
        if (!opened)
        {
            gchar *message = g_strdup_printf (_("Could not open link: %s"), error != NULL ? error->message : _("No browser configured"));
            gtk_label_set_text (GTK_LABEL (viewer->info), message);
            gtk_widget_set_tooltip_text (viewer->info, message);
            g_free (message);
        }
        g_clear_error (&error); g_free (uri);
    }
    g_clear_pointer (&viewer->pressed, g_free);
    NEEDOPENBGL
    return clicked;
}
static void draw_numbers (GtkWidget *widget, cairo_t *cr, E2_Viewer *viewer)
{
    GdkColor foreground, background;
    gdk_color_parse (e2_option_str_get ("dialog-view-foreground"), &foreground);
    gdk_color_parse (e2_option_str_get ("dialog-view-background"), &background);
    gdk_cairo_set_source_color (cr, &background); cairo_paint (cr);
    gdk_cairo_set_source_color (cr, &foreground);
    GtkTextView *view = GTK_TEXT_VIEW (widget);
    GdkRectangle visible;
    gtk_text_view_get_visible_rect (view, &visible);
    GtkTextIter iter;
    gtk_text_view_get_line_at_y (view, &iter, visible.y, NULL);
    PangoLayout *layout = gtk_widget_create_pango_layout (widget, NULL);
    pango_layout_set_font_description (layout, viewer->font);
    do
    {
        gint y, height, wx, wy, width;
        gtk_text_view_get_line_yrange (view, &iter, &y, &height);
        if (y > visible.y + visible.height) break;
        gtk_text_view_buffer_to_window_coords (view, GTK_TEXT_WINDOW_LEFT, 0, y, &wx, &wy);
        gchar number[32]; g_snprintf (number, sizeof (number), "%d", gtk_text_iter_get_line (&iter) + 1);
        pango_layout_set_text (layout, number, -1);
        pango_layout_get_pixel_size (layout, &width, NULL);
        cairo_move_to (cr, viewer->gutter - width - 6, wy);
        pango_cairo_show_layout (cr, layout);
    } while (gtk_text_iter_forward_line (&iter));
    g_object_unref (layout);
}
#ifdef USE_GTK3_0
static gboolean numbers_draw (GtkWidget *widget, cairo_t *cr, E2_Viewer *viewer)
{
    GdkWindow *window = gtk_text_view_get_window (GTK_TEXT_VIEW (widget), GTK_TEXT_WINDOW_LEFT);
    if (window != NULL && gtk_cairo_should_draw_window (cr, window))
    {
        cairo_save (cr);
        gtk_cairo_transform_to_window (cr, widget, window);
        cairo_rectangle (cr, 0, 0, viewer->gutter, gdk_window_get_height (window));
        cairo_clip (cr);
        draw_numbers (widget, cr, viewer);
        cairo_restore (cr);
    }
    return FALSE;
}
#else
static gboolean numbers_draw (GtkWidget *widget, GdkEventExpose *event, E2_Viewer *viewer)
{
    if (event->window == gtk_text_view_get_window (GTK_TEXT_VIEW (widget), GTK_TEXT_WINDOW_LEFT))
    {
        cairo_t *cr = gdk_cairo_create (event->window);
        gdk_cairo_rectangle (cr, &event->area); cairo_clip (cr);
        draw_numbers (widget, cr, viewer);
        cairo_destroy (cr);
    }
    return FALSE;
}
#endif
E2_Viewer *e2_viewer_new (gpointer bytes, gsize length)
{
    E2_Viewer *viewer = g_new0 (E2_Viewer, 1);
    viewer->bytes = bytes; viewer->length = length;
    viewer->decoded = e2_viewer_decode (bytes, length, e2_option_bool_get ("dialog-view-ascii-art"), NULL);
    viewer->buffer = gtk_text_buffer_new (NULL);
    gtk_text_buffer_set_text (viewer->buffer, viewer->decoded.text, -1);
    return viewer;
}
void e2_viewer_free (E2_Viewer *viewer)
{
    if (viewer == NULL) return;
    g_free (viewer->bytes); g_free (viewer->decoded.text); g_free (viewer->pressed);
    if (viewer->links != NULL) g_ptr_array_free (viewer->links, TRUE);
    if (viewer->font != NULL) pango_font_description_free (viewer->font);
    g_object_unref (viewer->buffer);
    g_free (viewer);
}
GtkTextBuffer *e2_viewer_buffer (E2_Viewer *viewer) { return g_object_ref (viewer->buffer); }
const gchar *e2_viewer_encoding (E2_Viewer *viewer) { return viewer->decoded.encoding; }
gboolean e2_viewer_is_art (E2_Viewer *viewer) { return viewer->decoded.art != E2_VIEWER_PLAIN; }
GtkWidget *e2_viewer_scrolled (E2_Viewer *viewer, GtkWidget *box)
{
    viewer->scroll = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (viewer->scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (viewer->scroll), GTK_SHADOW_IN);
    gtk_box_pack_start (GTK_BOX (box), viewer->scroll, TRUE, TRUE, E2_PADDING);
    return viewer->scroll;
}
void e2_viewer_attach (E2_Viewer *viewer, GtkWidget *view, GtkWidget *box)
{
    viewer->view = view;
    gtk_widget_set_name (view, "file-viewer-text");
    gtk_text_view_set_buffer (GTK_TEXT_VIEW (view), viewer->buffer);
#ifdef USE_GTK3_0
    GdkRGBA fg, bg;
    gdk_rgba_parse (&fg, e2_option_str_get ("dialog-view-foreground"));
    gdk_rgba_parse (&bg, e2_option_str_get ("dialog-view-background"));
    gtk_widget_override_color (view, GTK_STATE_FLAG_NORMAL, &fg);
    gtk_widget_override_background_color (view, GTK_STATE_FLAG_NORMAL, &bg);
#ifdef USE_GTK3_16
    e2_widget_override_style (view, "textview, textview text {color:%s; background-color:%s;}",
        e2_option_str_get ("dialog-view-foreground"), e2_option_str_get ("dialog-view-background"));
#endif
#else
    GdkColor fg, bg;
    gdk_color_parse (e2_option_str_get ("dialog-view-foreground"), &fg);
    gdk_color_parse (e2_option_str_get ("dialog-view-background"), &bg);
    gtk_widget_modify_text (view, GTK_STATE_NORMAL, &fg);
    gtk_widget_modify_base (view, GTK_STATE_NORMAL, &bg);
#endif
    update_links (viewer);
    update_gutter (viewer);
    viewer->controls = g_object_new (e2_viewer_controls_get_type (), NULL);
    gtk_widget_set_name (GTK_WIDGET (viewer->controls), "file-viewer-controls");
    gtk_box_pack_end (GTK_BOX (box), GTK_WIDGET (viewer->controls), FALSE, TRUE, E2_PADDING_XSMALL);
    GtkWidget *bar = gtk_hbox_new (FALSE, VIEWER_CONTROL_SPACING);
    gtk_container_add (GTK_CONTAINER (viewer->controls), bar);
    GtkWidget *label = gtk_label_new (_("Encoding:"));
    gtk_box_pack_start (GTK_BOX (bar), label, FALSE, FALSE, 0);
    GtkWidget *combo = gtk_combo_box_text_new ();
    gtk_widget_set_name (combo, "file-viewer-encoding");
    gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _("Automatic"));
    for (guint i = 1; i < G_N_ELEMENTS (encodings); i++)
        gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), encodings[i]);
    gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
    gtk_widget_set_tooltip_text (combo, _("Override encoding detection for this file"));
    gtk_box_pack_start (GTK_BOX (bar), combo, FALSE, FALSE, 0);
    viewer->info = gtk_label_new ("");
    viewer->controls->info = viewer->info;
    gtk_widget_set_name (viewer->info, "file-viewer-info");
    gtk_label_set_ellipsize (GTK_LABEL (viewer->info), PANGO_ELLIPSIZE_END);
    gtk_misc_set_alignment (GTK_MISC (viewer->info), 0.0, 0.5);
    gtk_box_pack_start (GTK_BOX (bar), viewer->info, TRUE, TRUE, 0);
    update_info (viewer);
    g_signal_connect (combo, "changed", G_CALLBACK (encoding_changed), viewer);
    gtk_widget_add_events (view, GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    g_signal_connect (view, "button-press-event", G_CALLBACK (link_press), viewer);
    g_signal_connect (view, "button-release-event", G_CALLBACK (link_release), viewer);
    g_signal_connect (view, "motion-notify-event", G_CALLBACK (link_motion), viewer);
#ifdef USE_GTK3_0
    g_signal_connect_after (view, "draw", G_CALLBACK (numbers_draw), viewer);
#else
    g_signal_connect_after (view, "expose-event", G_CALLBACK (numbers_draw), viewer);
#endif
}
void e2_viewer_add_actions (E2_Viewer *viewer, GtkWidget *actions)
{
    g_object_ref (actions);
    gtk_container_remove (GTK_CONTAINER (gtk_widget_get_parent (actions)), actions);
    gtk_container_add (GTK_CONTAINER (viewer->controls), actions);
    g_object_unref (actions);
    gtk_button_box_set_layout (GTK_BUTTON_BOX (actions), GTK_BUTTONBOX_END);
}
