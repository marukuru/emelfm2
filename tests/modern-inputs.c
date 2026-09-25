/* Real focus/selection painting regression, preloaded by run-modern-inputs.sh. */
#include "emelfm2.h"
#include "e2_modern_ui.h"
#include "screenshot.h"
#include <string.h>

static GtkWidget *probe, *button, *inputs[4], *frames[4];
static GdkPixbuf *blurred, *focused[4];
static guint step;
static gboolean modern;

static GdkPixbuf *capture (const gchar *name)
{
    GtkAllocation a;
    gtk_widget_get_allocation (probe, &a);
    GdkPixbuf *pixels = e2_test_capture_window (gtk_widget_get_window (probe),
        0, 0, a.width, a.height);
    g_assert_nonnull (pixels);
    const gchar *directory = g_getenv ("E2_INPUT_CAPTURE_DIR");
    if (directory != NULL)
    {
        gchar *path = g_strdup_printf ("%s/%s-%s.png", directory,
            g_getenv ("E2_INPUT_TEST_CASE"), name);
        g_assert_true (gdk_pixbuf_save (pixels, path, "png", NULL, NULL));
        g_free (path);
    }
    return pixels;
}

static const guchar *pixel (GdkPixbuf *image, gint x, gint y)
{
    g_assert_cmpint (x, >=, 0);
    g_assert_cmpint (x, <, gdk_pixbuf_get_width (image));
    g_assert_cmpint (y, >=, 0);
    g_assert_cmpint (y, <, gdk_pixbuf_get_height (image));
    return gdk_pixbuf_get_pixels (image) + y * gdk_pixbuf_get_rowstride (image)
        + x * gdk_pixbuf_get_n_channels (image);
}

static void bounds (GtkWidget *widget, GtkAllocation *a)
{
    gtk_widget_get_allocation (widget, a);
    g_assert_true (gtk_widget_translate_coordinates (widget, probe, 0, 0, &a->x, &a->y));
}

static void check_backgrounds (GdkPixbuf *before, GdkPixbuf *after)
{
    for (guint i = 0; i < G_N_ELEMENTS (inputs); i++)
    {
        GtkAllocation a;
        bounds (inputs[i], &a);
        /* Blank space away from text, cursor, selection, buttons and borders. */
        for (gint y = a.y + a.height / 2 - 2; y < a.y + a.height / 2 + 2; y++)
            for (gint x = a.x + a.width / 2; x < a.x + a.width / 2 + 12; x++)
                g_assert_cmpint (memcmp (pixel (before, x, y), pixel (after, x, y), 3), ==, 0);
    }
}

static void check_border (GdkPixbuf *before, GdkPixbuf *after, guint input)
{
    GtkAllocation a;
    bounds (frames[input], &a);
    guint changed = 0;
    for (gint y = a.y; y < a.y + 3; y++)
        for (gint x = a.x + a.width / 3; x < a.x + a.width * 2 / 3; x++)
            changed += memcmp (pixel (before, x, y), pixel (after, x, y), 3) != 0;
    g_assert_cmpuint (changed, >, 0);
}

static void check_selection (GdkPixbuf *before, GdkPixbuf *after, guint input)
{
    GtkAllocation a;
    bounds (inputs[input], &a);
    guint changed = 0;
    for (gint y = a.y + 4; y < a.y + MIN (a.height - 4, 26); y++)
        for (gint x = a.x + 8; x < a.x + 90; x++)
            changed += memcmp (pixel (before, x, y), pixel (after, x, y), 3) != 0;
    g_assert_cmpuint (changed, >, 40);
    if (modern) check_backgrounds (blurred, after);
}

static void focus_input (guint input)
{
    gtk_widget_grab_focus (inputs[input]);
    if (GTK_IS_ENTRY (inputs[input]))
        gtk_editable_select_region (GTK_EDITABLE (inputs[input]), 0, 0);
}

static gboolean tick (gpointer unused)
{
    if (curr_view == NULL || curr_view->dir[0] == '\0'
        || g_atomic_int_get (&app.pane1.view.listcontrols.cd_working)
        || g_atomic_int_get (&app.pane2.view.listcontrols.cd_working)) return TRUE;
    CLOSEBGL
    if (step == 0)
    {
        modern = e2_modern_ui_enabled ();
        probe = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        gtk_window_set_default_size (GTK_WINDOW (probe), 420, 340);
        GtkWidget *box = gtk_vbox_new (FALSE, 12);
        gtk_container_set_border_width (GTK_CONTAINER (box), 12);
        gtk_container_add (GTK_CONTAINER (probe), box);
        button = gtk_button_new_with_label ("Focus elsewhere");
        gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);
        inputs[0] = frames[0] = gtk_entry_new ();
        gtk_entry_set_text (GTK_ENTRY (inputs[0]), "Text field");
        gtk_box_pack_start (GTK_BOX (box), inputs[0], FALSE, FALSE, 0);
#ifdef USE_GTK3_0
        GtkWidget *combo = gtk_combo_box_text_new_with_entry ();
#else
        GtkWidget *combo = gtk_combo_box_entry_new_text ();
#endif
        inputs[1] = frames[1] = gtk_bin_get_child (GTK_BIN (combo));
        gtk_entry_set_text (GTK_ENTRY (inputs[1]), "Path field");
        gtk_box_pack_start (GTK_BOX (box), combo, FALSE, FALSE, 0);
        inputs[2] = frames[2] = gtk_spin_button_new_with_range (0, 100, 1);
        gtk_box_pack_start (GTK_BOX (box), inputs[2], FALSE, FALSE, 0);
        frames[3] = gtk_scrolled_window_new (NULL, NULL);
        gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (frames[3]), GTK_SHADOW_IN);
        inputs[3] = gtk_text_view_new ();
        gtk_text_buffer_set_text (gtk_text_view_get_buffer (GTK_TEXT_VIEW (inputs[3])),
            "Text area\nSecond line", -1);
        gtk_container_add (GTK_CONTAINER (frames[3]), inputs[3]);
        gtk_box_pack_start (GTK_BOX (box), frames[3], TRUE, TRUE, 0);
        for (guint i = 0; i < 3; i++) gtk_widget_set_name (inputs[i], "e2-focus-test-input");
        gtk_widget_show_all (probe);
        gtk_widget_grab_focus (button);
        gtk_window_present (GTK_WINDOW (probe));
        gdk_window_focus (gtk_widget_get_window (probe), GDK_CURRENT_TIME);
    }
    else if (step == 1)
    {
        blurred = capture ("blurred");
        focus_input (0);
    }
    else if (step >= 2 && step <= 5)
    {
        guint input = step - 2;
        g_assert_true (gtk_widget_has_focus (inputs[input]));
        gchar *name = g_strdup_printf ("focus-%u", input);
        focused[input] = capture (name);
        g_free (name);
        if (modern)
        {
            check_backgrounds (blurred, focused[input]);
            check_border (blurred, focused[input], input);
        }
        if (input < 3) focus_input (input + 1);
        else
        {
            focus_input (0);
            gtk_editable_select_region (GTK_EDITABLE (inputs[0]), 0, -1);
        }
    }
    else if (step == 6)
    {
        GdkPixbuf *selected = capture ("entry-selection");
        check_selection (focused[0], selected, 0);
        g_object_unref (selected);
        gtk_editable_select_region (GTK_EDITABLE (inputs[0]), 0, 0);
        focus_input (3);
        GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (inputs[3]));
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds (buffer, &start, &end);
        gtk_text_buffer_select_range (buffer, &start, &end);
    }
    else if (step == 7)
    {
        GdkPixbuf *selected = capture ("area-selection");
        check_selection (focused[3], selected, 3);
        g_object_unref (selected);
        GtkTextBuffer *buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (inputs[3]));
        GtkTextIter start;
        gtk_text_buffer_get_start_iter (buffer, &start);
        gtk_text_buffer_place_cursor (buffer, &start);
        gtk_widget_grab_focus (button);
    }
    else
    {
        GdkPixbuf *after = capture ("blurred-again");
        if (modern)
        {
            check_backgrounds (blurred, after);
            for (guint i = 0; i < 4; i++) check_border (focused[i], after, i);
        }
        g_object_unref (after);
        g_object_unref (blurred);
        for (guint i = 0; i < 4; i++) g_object_unref (focused[i]);
        gtk_widget_destroy (probe);
        g_print ("input backgrounds, focus borders and text selections passed\n");
        e2_main_closedown (TRUE, TRUE, TRUE);
        return FALSE;
    }
    step++;
    OPENBGL
    return TRUE;
}

__attribute__((constructor)) static void schedule (void)
{ unsetenv ("LD_PRELOAD"); g_timeout_add (500, tick, NULL); }
