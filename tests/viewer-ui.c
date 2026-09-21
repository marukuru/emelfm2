/* Exercise the production viewer in a private application profile. */
#include "emelfm2.h"
#include "e2_option.h"
#include "e2_view_dialog.h"
#include "e2_config_dialog.h"
#include "e2_button.h"
#include <glib/gstdio.h>
#include <pango/pangofc-font.h>
static GtkWidget *dialog, *view;
static guint step;
static gchar *root, *marker;
static gint64 wait_until;
static GtkWidget *find (GtkWidget *widget, const gchar *name)
{
    if (!g_strcmp0 (gtk_widget_get_name (widget), name)) return widget;
    if (!GTK_IS_CONTAINER (widget)) return NULL;
    GList *children = gtk_container_get_children (GTK_CONTAINER (widget));
    GtkWidget *found = NULL;
    for (GList *p = children; p != NULL && found == NULL; p = p->next) found = find (p->data, name);
    g_list_free (children); return found;
}
static void open_viewer (const gchar *name)
{
    gchar *path = g_build_filename (root, name, NULL);
#ifdef E2_VFS
    VPATH file = {path, NULL};
    g_assert_true (e2_view_dialog_create (&file));
#else
    g_assert_true (e2_view_dialog_create (path));
#endif
    g_free (path);
    GList *windows = gtk_window_list_toplevels ();
    dialog = view = NULL;
    for (GList *p = windows; p != NULL; p = p->next)
        if ((view = find (p->data, "file-viewer-text")) != NULL) { dialog = p->data; break; }
    g_list_free (windows);
    g_assert_nonnull (view);
    gtk_window_move (GTK_WINDOW (dialog), 0, 0);
}
static void close_viewer (void)
{
    gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE);
    dialog = view = NULL;
}
static void capture (const gchar *name)
{
    const gchar *directory = g_getenv ("E2_VIEWER_CAPTURE_DIR");
    if (directory == NULL) return;
    g_assert_cmpint (g_mkdir_with_parents (directory, 0700), ==, 0);
    GtkAllocation size; gtk_widget_get_allocation (dialog, &size);
    GdkWindow *window = gtk_widget_get_window (dialog);
#ifdef USE_GTK3_0
    GdkPixbuf *pixels = gdk_pixbuf_get_from_window (window, 0, 0, size.width, size.height);
#else
    GdkPixbuf *pixels = gdk_pixbuf_get_from_drawable (NULL, window,
        gtk_widget_get_colormap (dialog), 0, 0, 0, 0, size.width, size.height);
#endif
    gchar *path = g_build_filename (directory, name, NULL);
    if (pixels != NULL) { gdk_pixbuf_save (pixels, path, "png", NULL, NULL); g_object_unref (pixels); }
    g_free (path);
}
static gchar *content (void)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds (buffer, &start, &end);
    return gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
}
static void font_is (const gchar *family, const gchar *file)
{
    PangoContext *context = gtk_widget_get_pango_context (view);
    const PangoFontDescription *description = pango_context_get_font_description (context);
    g_assert_cmpstr (pango_font_description_get_family (description), ==, family);
    if (file != NULL)
    {
        PangoFont *font = pango_context_load_font (context, description);
        g_assert_true (PANGO_IS_FC_FONT (font));
        FcChar8 *path = NULL;
        g_assert_cmpint (FcPatternGetString (pango_fc_font_get_pattern (PANGO_FC_FONT (font)), FC_FILE, 0, &path), ==, FcResultMatch);
        g_assert_true (g_str_has_suffix ((const gchar *)path, file));
        const gchar *expected = g_getenv ("E2_VIEWER_EXPECT_FONT_DIR");
        if (expected != NULL)
        {
            gchar *canonical = g_canonicalize_filename ((const gchar *)path, NULL);
            g_assert_true (g_str_has_prefix (canonical, expected));
            g_free (canonical);
        }
        g_object_unref (font);
    }
}
static void coords (gint offset, gint *x, gint *y)
{
    GtkTextIter iter;
    gtk_text_buffer_get_iter_at_offset (gtk_text_view_get_buffer (GTK_TEXT_VIEW (view)), &iter, offset);
    GdkRectangle rect;
    gtk_text_view_get_iter_location (GTK_TEXT_VIEW (view), &iter, &rect);
    gtk_text_view_buffer_to_window_coords (GTK_TEXT_VIEW (view), GTK_TEXT_WINDOW_TEXT,
        rect.x + MAX (1, rect.width / 2), rect.y + rect.height / 2, x, y);
}
static void event (GdkEventType type, gint x, gint y, GdkModifierType state)
{
    GdkEvent *e = gdk_event_new (type);
    GdkWindow *window = gtk_text_view_get_window (GTK_TEXT_VIEW (view), GTK_TEXT_WINDOW_TEXT);
    if (type == GDK_MOTION_NOTIFY)
    {
        e->motion.window = g_object_ref (window); e->motion.x = x; e->motion.y = y;
        e->motion.state = state; e->motion.time = GDK_CURRENT_TIME;
    }
    else
    {
        e->button.window = g_object_ref (window); e->button.x = x; e->button.y = y;
        e->button.state = state; e->button.button = 1; e->button.time = GDK_CURRENT_TIME;
    }
#ifdef USE_GTK3_0
    GdkDeviceManager *manager = gdk_display_get_device_manager (gtk_widget_get_display (view));
    gdk_event_set_device (e, gdk_device_manager_get_client_pointer (manager));
#endif
    gtk_main_do_event (e); gdk_event_free (e);
}
static void click (gint extra, gboolean drag, GdkModifierType modifier)
{
    gint x, y; coords (2, &x, &y); x += extra;
    event (GDK_BUTTON_PRESS, x, y, modifier);
    if (drag) { x += 40; event (GDK_MOTION_NOTIFY, x, y, GDK_BUTTON1_MASK); }
    event (GDK_BUTTON_RELEASE, x, y, modifier);
}
/* Compare actual widget positions after the window manager has resized the
 * viewer. Checking requisitions alone would miss the collapsed filename bug. */
static void check_layout (gboolean wrapped)
{
    GtkWidget *encoding = find (dialog, "file-viewer-encoding");
    GtkWidget *info = find (dialog, "file-viewer-info");
    GtkWidget *toggle = g_object_get_data (G_OBJECT (view), "viewer-wrap-toggle");
    GtkWidget *close = gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE);
    GtkWidget *filename = g_object_get_data (G_OBJECT (dialog), "e2-dialog-label");
    GtkAllocation size, title, combo, button;
    gtk_widget_get_allocation (dialog, &size);
    gtk_widget_get_allocation (filename, &title);
    gtk_widget_get_allocation (encoding, &combo);
    gtk_widget_get_allocation (close, &button);
    gint x, encoding_y, button_y, wrap_y, info_y;
    g_assert_true (gtk_widget_translate_coordinates (encoding, dialog, 0, 0, &x, &encoding_y));
    g_assert_true (gtk_widget_translate_coordinates (close, dialog, 0, 0, &x, &button_y));
    g_assert_true (gtk_widget_translate_coordinates (toggle, dialog, 0, 0, &x, &wrap_y));
    g_assert_true (gtk_widget_translate_coordinates (info, dialog, 0, 0, &x, &info_y));
    g_assert_cmpint (title.width, >, size.width - 60);
    g_assert_cmpint (ABS (wrap_y - button_y), <, button.height);
    g_assert_cmpint (ABS (encoding_y - info_y), <, combo.height);
    g_assert_false (pango_layout_is_ellipsized (gtk_label_get_layout (GTK_LABEL (info))));
    if (wrapped) g_assert_cmpint (button_y, >=, encoding_y + combo.height);
    else g_assert_cmpint (ABS (encoding_y + combo.height / 2 - button_y - button.height / 2), <=, 2);
    g_assert_cmpint (button_y + button.height, <=, size.height);
    gchar *expected = g_build_filename (root, "100% <notes> & longer file viewer filename.txt", NULL);
    g_assert_cmpstr (gtk_label_get_text (GTK_LABEL (filename)), ==, expected);
    g_assert_cmpint (pango_layout_is_ellipsized (gtk_label_get_layout (GTK_LABEL (filename))), ==, wrapped);
    g_free (expected);
}
static gboolean tick (gpointer data)
{
    if (curr_view == NULL || curr_view->dir[0] == 0 || g_atomic_int_get (&curr_view->listcontrols.cd_working)) return TRUE;
    CLOSEBGL
    switch (step)
    {
        case 0:
            root = g_strdup (g_getenv ("E2_VIEWER_TEST")); marker = g_build_filename (root, "opened", NULL);
            g_assert_cmpstr (e2_option_get ("dialog-view-ascii-art")->group, ==, "interface.File viewer");
            e2_option_bool_set ("dialog-view-use-font", TRUE);
            e2_option_str_set_direct (e2_option_get ("dialog-view-font"), "DejaVu Sans Mono 11");
            e2_option_bool_set ("dialog-view-line-numbers", TRUE);
            e2_option_bool_set ("dialog-view-custom-browser", TRUE);
            e2_option_bool_set ("dialog-view-wrap", FALSE);
            e2_option_int_set ("dialog-view-max-width", 40);
            e2_option_color_set_str ("dialog-view-foreground", "#eeeecc");
            e2_option_color_set_str ("dialog-view-background", "#202030");
            e2_option_color_set_str ("dialog-view-link-color", "#00ff66");
            gchar *browser = g_build_filename (root, "fake browser", NULL);
            e2_option_str_set_direct (e2_option_get ("dialog-view-browser"), browser); g_free (browser);
            open_viewer ("plain.txt");
            gtk_window_resize (GTK_WINDOW (dialog), 1000, 500);
            break;
        case 1:
        {
            font_is ("DejaVu Sans Mono", NULL);
            gchar *text = content ();
            g_assert_true (g_str_has_prefix (text, "https://example.test/a?x=$(id)&b=1\n"));
            g_assert_nonnull (strstr (text, "日本語"));
            g_free (text);
            gint gutter = gtk_text_view_get_border_window_size (GTK_TEXT_VIEW (view), GTK_TEXT_WINDOW_LEFT);
            g_assert_cmpint (gutter, >, 0);
            PangoLayout *layout = gtk_widget_create_pango_layout (view, "M");
            gint width; pango_layout_get_pixel_size (layout, &width, NULL); g_object_unref (layout);
            GdkRectangle visible; gtk_text_view_get_visible_rect (GTK_TEXT_VIEW (view), &visible);
            g_assert_cmpint (visible.width, <=, 40 * width + 4);
            GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (view));
            GtkTextTag *link = gtk_text_tag_table_lookup (gtk_text_buffer_get_tag_table (buffer), "viewer-link");
            GdkColor *color;
            g_object_get (link, "foreground-gdk", &color, NULL);
            g_assert_cmpint (color->green, ==, 65535); g_assert_cmpint (color->red, ==, 0);
            gdk_color_free (color);
            click (0, FALSE, 0);
            wait_until = g_get_monotonic_time () + 5000000;
            break;
        }
        case 2:
        {
            if (!g_file_test (marker, G_FILE_TEST_EXISTS))
            { g_assert_cmpint (g_get_monotonic_time (), <, wait_until); goto wait; }
            gchar *opened; g_assert_true (g_file_get_contents (marker, &opened, NULL, NULL));
            g_assert_cmpstr (opened, ==, "1\nhttps://example.test/a?x=$(id)&b=1\n"); g_free (opened);
            g_unlink (marker);
            click (0, TRUE, 0);
            click (0, FALSE, GDK_SHIFT_MASK);
            gint x, y; coords (31, &x, &y);
            event (GDK_BUTTON_PRESS, x + 80, y, 0); event (GDK_BUTTON_RELEASE, x + 80, y, 0);
            wait_until = g_get_monotonic_time () + 400000;
            break;
        }
        case 3:
            if (g_get_monotonic_time () < wait_until) goto wait;
            g_assert_false (g_file_test (marker, G_FILE_TEST_EXISTS));
            e2_option_bool_set ("dialog-view-custom-browser", FALSE);
            click (0, FALSE, 0);
            wait_until = g_get_monotonic_time () + 5000000;
            break;
        case 4:
            if (!g_file_test (marker, G_FILE_TEST_EXISTS))
            { g_assert_cmpint (g_get_monotonic_time (), <, wait_until); goto wait; }
            g_unlink (marker);
            close_viewer (); open_viewer ("pc.nfo");
            break;
        case 5:
        {
            font_is ("PxPlus IBM VGA 8x16", "IBM_VGA_8x16.ttf");
            capture ("pc.png");
            g_assert_cmpint (gtk_text_view_get_wrap_mode (GTK_TEXT_VIEW (view)), ==, GTK_WRAP_NONE);
            gchar *text = content (); g_assert_nonnull (strstr (text, "╔═══╗")); g_free (text);
            g_assert_nonnull (strstr (gtk_label_get_text (GTK_LABEL (find (dialog, "file-viewer-info"))), "CP437"));
            close_viewer (); open_viewer ("amiga.nfo");
            break;
        }
        case 6:
        {
            font_is ("Topaz a600a1200a400", "Topaz_a1200.ttf");
            capture ("amiga.png");
            gchar *text = content (); g_assert_nonnull (strstr (text, "ÆØØØ")); g_free (text);
            GtkWidget *encoding = find (dialog, "file-viewer-encoding");
            gtk_combo_box_set_active (GTK_COMBO_BOX (encoding), 6);
            text = content (); g_assert_null (strstr (text, "ÆØØØ")); g_free (text);
            gtk_combo_box_set_active (GTK_COMBO_BOX (encoding), 0);
            text = content (); g_assert_nonnull (strstr (text, "ÆØØØ")); g_free (text);
            close_viewer ();
            e2_option_bool_set ("dialog-view-ascii-art", FALSE);
            e2_option_bool_set ("dialog-view-links", FALSE);
            e2_option_bool_set ("dialog-view-line-numbers", FALSE);
            open_viewer ("plain.txt");
            break;
        }
        case 7:
            g_assert_cmpint (gtk_text_view_get_border_window_size (GTK_TEXT_VIEW (view), GTK_TEXT_WINDOW_LEFT), ==, 0);
            click (0, FALSE, 0);
            wait_until = g_get_monotonic_time () + 400000;
            break;
        case 8:
            if (g_get_monotonic_time () < wait_until) goto wait;
            g_assert_false (g_file_test (marker, G_FILE_TEST_EXISTS));
            close_viewer (); open_viewer ("pc.nfo");
            break;
        case 9:
            font_is ("DejaVu Sans Mono", NULL);
            close_viewer (); open_viewer ("utf16.txt");
            break;
        case 10:
        {
            gchar *text = content (); g_assert_cmpstr (text, ==, "Hello 世界\nlast line\n"); g_free (text);
            close_viewer ();
            open_viewer ("100% <notes> & longer file viewer filename.txt");
            gtk_window_resize (GTK_WINDOW (dialog), 1000, 500);
            break;
        }
        case 11:
        {
            check_layout (FALSE);
            capture ("layout-wide.png");
            GtkWidget *toggle = g_object_get_data (G_OBJECT (view), "viewer-wrap-toggle");
            gtk_button_clicked (GTK_BUTTON (toggle));
            g_assert_cmpint (gtk_text_view_get_wrap_mode (GTK_TEXT_VIEW (view)), ==, GTK_WRAP_WORD);
            gtk_button_clicked (GTK_BUTTON (toggle));
            g_assert_cmpint (gtk_text_view_get_wrap_mode (GTK_TEXT_VIEW (view)), ==, GTK_WRAP_NONE);
            gtk_window_resize (GTK_WINDOW (dialog), 360, 500);
            break;
        }
        case 12:
            check_layout (TRUE);
            capture ("layout-narrow.png");
            gtk_window_resize (GTK_WINDOW (dialog), 1000, 500);
            break;
        case 13:
            check_layout (FALSE);
            /* Moving the action area must preserve the Find and Hide responses. */
            gtk_button_clicked (GTK_BUTTON (gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), E2_RESPONSE_FIND)));
            break;
        case 14:
            check_layout (FALSE);
            g_assert_true (gtk_widget_get_mapped (gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), E2_RESPONSE_USER3)));
            gtk_button_clicked (GTK_BUTTON (gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), E2_RESPONSE_USER3)));
            break;
        case 15:
            g_assert_false (gtk_widget_get_mapped (gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), E2_RESPONSE_USER3)));
            gtk_button_clicked (GTK_BUTTON (gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE)));
            dialog = view = NULL;
            e2_config_dialog_create ("File viewer");
            break;
        case 16:
        {
            /* Verify the requested page is usable, not merely registered. */
            E2_OptionSet *width = e2_option_get ("dialog-view-max-width");
            g_assert_true (GTK_IS_SPIN_BUTTON (width->widget));
            g_assert_true (gtk_widget_get_mapped (width->widget));
            g_assert_true (GTK_IS_TOGGLE_BUTTON (e2_option_get ("dialog-view-ascii-art")->widget));
            dialog = gtk_widget_get_toplevel (width->widget);
            capture ("settings.png");
            gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
            gchar *done = g_build_filename (root, "passed", NULL);
            g_file_set_contents (done, "passed", -1, NULL); g_free (done);
            e2_main_closedown (TRUE, TRUE, TRUE);
            return FALSE;
        }
    }
    fprintf (stderr, "viewer-ui: step %u passed\n", step++);
wait:
    OPENBGL
    return TRUE;
}
__attribute__((constructor)) static void schedule (void)
{ unsetenv ("LD_PRELOAD"); g_timeout_add (150, tick, NULL); }
