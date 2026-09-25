/* Verify screenshot pixels, cropping and GTK 3 cached-surface safety. */
#include "screenshot.h"

static GtkWidget *window;
static guint step;

static void paint (cairo_t *cr)
{
    cairo_set_source_rgb (cr, 0, step != 0, step == 0);
    cairo_paint (cr);
    cairo_set_source_rgb (cr, 1, 0, 0);
    cairo_rectangle (cr, 0, 0, 30, 20);
    cairo_fill (cr);
}

#if GTK_MAJOR_VERSION >= 3
static gboolean draw (GtkWidget *widget, cairo_t *cr, gpointer unused)
{ paint (cr); return FALSE; }
#else
static gboolean expose (GtkWidget *widget, GdkEventExpose *event, gpointer unused)
{
    cairo_t *cr = gdk_cairo_create (gtk_widget_get_window (widget));
    paint (cr);
    cairo_destroy (cr);
    return FALSE;
}
#endif

static void check_pixel (GdkPixbuf *pixels, gint x, gint y, gint r, gint g, gint b)
{
    const guchar *p = gdk_pixbuf_get_pixels (pixels)
        + y * gdk_pixbuf_get_rowstride (pixels) + x * gdk_pixbuf_get_n_channels (pixels);
    g_assert_cmpint (p[0], ==, r);
    g_assert_cmpint (p[1], ==, g);
    g_assert_cmpint (p[2], ==, b);
}

static gboolean capture (gpointer unused)
{
    GdkWindow *native = gtk_widget_get_window (window);
#if GTK_MAJOR_VERSION >= 3
    /* Reproduce the Cairo precondition that aborts the old capture path,
     * without relying on a theme or rendering backend to populate the cache. */
    cairo_surface_t *scratch = cairo_image_surface_create (CAIRO_FORMAT_RGB24, 1, 1);
    cairo_t *source = cairo_create (scratch);
    gdk_cairo_set_source_window (source, native, 0, 0);
    cairo_surface_t *surface;
    g_assert_cmpint (cairo_pattern_get_surface (cairo_get_source (source), &surface), ==, CAIRO_STATUS_SUCCESS);
    static const unsigned char id[] = "emelfm2-screenshot-test";
    g_assert_cmpint (cairo_surface_set_mime_data (surface, CAIRO_MIME_TYPE_UNIQUE_ID,
        id, sizeof (id), NULL, NULL), ==, CAIRO_STATUS_SUCCESS);
#endif
    GdkPixbuf *full = e2_test_capture_window (native, 0, 0, 120, 90);
    GdkPixbuf *crop = e2_test_capture_window (native, 40, 30, 35, 25);
    g_assert_nonnull (full);
    g_assert_nonnull (crop);
    g_assert_cmpint (gdk_pixbuf_get_width (full), ==, 120);
    g_assert_cmpint (gdk_pixbuf_get_height (full), ==, 90);
    g_assert_cmpint (gdk_pixbuf_get_width (crop), ==, 35);
    g_assert_cmpint (gdk_pixbuf_get_height (crop), ==, 25);
    check_pixel (full, 10, 10, 255, 0, 0);
    check_pixel (full, 60, 50, 0, step ? 255 : 0, step ? 0 : 255);
    check_pixel (crop, 0, 0, 0, step ? 255 : 0, step ? 0 : 255);
    check_pixel (crop, 34, 24, 0, step ? 255 : 0, step ? 0 : 255);
    g_object_unref (full);
    g_object_unref (crop);
#if GTK_MAJOR_VERSION >= 3
    cairo_destroy (source);
    cairo_surface_destroy (scratch);
#endif
    if (step++ == 0)
    {
        gtk_widget_queue_draw (window);
        return TRUE;
    }
    gtk_widget_destroy (window);
    gtk_main_quit ();
    g_print ("screenshot pixels, crop offsets, repaint and cached surfaces passed\n");
    return FALSE;
}

int main (int argc, char **argv)
{
    gtk_init (&argc, &argv);
    g_log_set_always_fatal (G_LOG_FATAL_MASK | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL);
    window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated (GTK_WINDOW (window), FALSE);
    gtk_window_move (GTK_WINDOW (window), 71, 53);
    gtk_window_set_default_size (GTK_WINDOW (window), 120, 90);
    GtkWidget *area = gtk_drawing_area_new ();
    gtk_container_add (GTK_CONTAINER (window), area);
#if GTK_MAJOR_VERSION >= 3
    g_signal_connect (area, "draw", G_CALLBACK (draw), NULL);
#else
    g_signal_connect (area, "expose-event", G_CALLBACK (expose), NULL);
#endif
    gtk_widget_show_all (window);
    g_timeout_add (500, capture, NULL);
    gtk_main ();
    return 0;
}
