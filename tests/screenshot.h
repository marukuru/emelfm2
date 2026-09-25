#ifndef E2_TEST_SCREENSHOT_H
#define E2_TEST_SCREENSHOT_H

#include <gtk/gtk.h>

/* These tests run in a private Xvfb display with the target unobscured.
 * GTK 3 versions shipped with Ubuntu 22.04 mark a window's Cairo surface
 * dirty without first flushing it in gdk_pixbuf_get_from_window(). That
 * aborts when the renderer has attached MIME/cache data to the surface.
 * Read the corresponding screen region instead, leaving GTK's cached
 * window surface alone.
 */
static GdkPixbuf *e2_test_capture_window (GdkWindow *window,
    gint x, gint y, gint width, gint height)
{
#if GTK_MAJOR_VERSION >= 3
    g_assert_true (gdk_window_is_viewable (window));
    gint root_x, root_y;
    gdk_window_get_root_coords (window, x, y, &root_x, &root_y);
    gdk_display_sync (gdk_window_get_display (window));
    return gdk_pixbuf_get_from_window (
        gdk_screen_get_root_window (gdk_window_get_screen (window)),
        root_x, root_y, width, height);
#else
    return gdk_pixbuf_get_from_drawable (NULL, window, NULL,
        x, y, 0, 0, width, height);
#endif
}

#endif
