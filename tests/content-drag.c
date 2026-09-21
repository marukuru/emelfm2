/* Send real pointer events under Xvfb and intercept window-move requests. */
#include "emelfm2.h"
#include "e2_window.h"
#include <gdk/gdkx.h>
#include <gmodule.h>

static guint step, main_moves, other_moves, clicks;
static GtkWidget *probe, *probe_bar, *bar, *button;
static gint drag_x, drag_y;
static Display *display;
static Bool (*move_pointer) (Display *, int, int, int, unsigned long);
static Bool (*press_button) (Display *, unsigned int, Bool, unsigned long);

/* No window manager is needed: these are the requests GTK would send to it. */
void gdk_window_begin_move_drag (GdkWindow *window, gint button,
    gint x, gint y, guint32 time)
{
    if (app.main_window != NULL && window == gtk_widget_get_window (app.main_window)) main_moves++;
    else other_moves++;
}
#ifdef USE_GTK3_0
void gdk_window_begin_move_drag_for_device (GdkWindow *window, GdkDevice *device,
    gint button, gint x, gint y, guint32 time)
{ gdk_window_begin_move_drag (window, button, x, y, time); }
#endif

static void drag_theme (void)
{
#ifdef USE_GTK3_0
    GtkCssProvider *provider = gtk_css_provider_new ();
    gtk_css_provider_load_from_data (provider, "* { -GtkWidget-window-dragging: true; }", -1, NULL);
    gtk_style_context_add_provider_for_screen (gdk_screen_get_default (),
        GTK_STYLE_PROVIDER (provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref (provider);
#else
    gtk_rc_parse_string (
        "style \"test-content-drag\" { GtkWidget::window-dragging = 1"
        " GtkMenuShell::window-dragging = 1 GtkToolbar::window-dragging = 1"
        " GtkMenuBar::window-dragging = 1 }"
        "widget \"*\" style : application \"test-content-drag\"");
#endif
}
static gboolean draggable (GtkWidget *widget)
{
    gboolean value = FALSE;
    if (gtk_widget_class_find_style_property (GTK_WIDGET_GET_CLASS (widget), "window-dragging") != NULL)
        gtk_widget_style_get (widget, "window-dragging", &value, NULL);
    return value;
}
static void check_content (GtkWidget *widget, gpointer unused)
{
    if (draggable (widget)) g_error ("Draggable main-window content: %s", G_OBJECT_TYPE_NAME (widget));
    if (GTK_IS_CONTAINER (widget)) gtk_container_forall (GTK_CONTAINER (widget), check_content, NULL);
}
static GtkWidget *empty_bar (void)
{
    GtkWidget *toolbar = gtk_toolbar_new ();
    gtk_widget_set_size_request (toolbar, 200, 40);
    return toolbar;
}
static void press (GtkWidget *widget)
{
    GtkWidget *top = gtk_widget_get_toplevel (widget);
    GtkAllocation size;
    gint x, y;
    gtk_widget_get_allocation (widget, &size);
    g_assert_true (gtk_widget_translate_coordinates (widget, top,
        size.width / 2, size.height / 2, &x, &y));
    gdk_window_get_origin (gtk_widget_get_window (top), &drag_x, &drag_y);
    drag_x += x; drag_y += y;
    move_pointer (display, -1, drag_x, drag_y, CurrentTime);
    press_button (display, 1, True, CurrentTime);
    XFlush (display);
}
static void motion (void)
{
    move_pointer (display, -1, drag_x + 40, drag_y + 15, CurrentTime);
    XFlush (display);
}
static void release (void)
{ press_button (display, 1, False, CurrentTime); XFlush (display); }
static void clicked (GtkToolButton *widget, gpointer unused)
{ clicks++; }
static gboolean tick (gpointer unused)
{
    if (curr_view == NULL || curr_view->dir[0] == 0
        || g_atomic_int_get (&curr_view->listcontrols.cd_working)) return TRUE;
    CLOSEBGL
    switch (step)
    {
        case 0:
        {
            GModule *xtest = g_module_open ("libXtst.so.6", G_MODULE_BIND_LAZY);
            g_assert_nonnull (xtest);
            g_assert_true (g_module_symbol (xtest, "XTestFakeMotionEvent", (gpointer *)&move_pointer));
            g_assert_true (g_module_symbol (xtest, "XTestFakeButtonEvent", (gpointer *)&press_button));
            g_module_make_resident (xtest); g_module_close (xtest);
            display = gdk_x11_display_get_xdisplay (gdk_display_get_default ());
            drag_theme ();
            gtk_window_resize (GTK_WINDOW (app.main_window), 700, 600);
            gtk_window_move (GTK_WINDOW (app.main_window), 0, 0);
            bar = empty_bar ();
            gtk_box_pack_start (GTK_BOX (app.vbox_main), bar, FALSE, FALSE, 0);
            gtk_box_reorder_child (GTK_BOX (app.vbox_main), bar, 0);
            gtk_widget_show (bar);
            probe = gtk_window_new (GTK_WINDOW_TOPLEVEL);
            probe_bar = empty_bar ();
            gtk_container_add (GTK_CONTAINER (probe), probe_bar);
            gtk_window_move (GTK_WINDOW (probe), 900, 20);
            gtk_widget_show_all (probe);
            break;
        }
        case 1:
            /* The same theme must still enable dragging outside our content. */
            g_assert_true (draggable (probe_bar));
            press (probe_bar);
            break;
        case 2: motion (); break;
        case 3: release (); break;
        case 4:
            g_assert_cmpuint (other_moves, >, 0);
            fprintf (stderr, "positive control: %u window-move requests\n", other_moves);
            check_content (app.hbox_main, NULL);
            g_assert_false (draggable (app.main_window));
            press (bar);
            break;
        case 5: motion (); break;
        case 6: release (); break;
        case 7:
            g_assert_cmpuint (main_moves, ==, 0);
            e2_window_recreate (&app.window);
            drag_theme (); //theme changes must not re-enable content dragging
            break;
        case 8:
            check_content (app.hbox_main, NULL);
            press (bar);
            break;
        case 9: motion (); break;
        case 10: release (); break;
        case 11:
            g_assert_cmpuint (main_moves, ==, 0);
            button = GTK_WIDGET (gtk_tool_button_new (NULL, "Test button"));
            gtk_toolbar_insert (GTK_TOOLBAR (bar), GTK_TOOL_ITEM (button), -1);
            g_signal_connect (button, "clicked", G_CALLBACK (clicked), NULL);
            gtk_widget_show_all (bar);
            break;
        case 12: press (button); release (); break;
        case 13:
        {
            g_assert_cmpuint (clicks, ==, 1);
            g_assert_cmpuint (main_moves, ==, 0);
            gtk_widget_destroy (probe);
            gchar *done = g_build_filename (g_getenv ("E2_CONTENT_DRAG_TEST"), "passed", NULL);
            g_assert_true (g_file_set_contents (done, "passed", -1, NULL));
            g_free (done);
            e2_main_closedown (TRUE, TRUE, TRUE);
            return FALSE;
        }
    }
    fprintf (stderr, "content drag: step %u passed\n", step++);
    OPENBGL
    return TRUE;
}
__attribute__((constructor)) static void schedule (void)
{ unsetenv ("LD_PRELOAD"); g_timeout_add (200, tick, NULL); }
