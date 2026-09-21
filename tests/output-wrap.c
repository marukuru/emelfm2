/* Exercise wrapping in real command logs, including both VTE pane logs. */
#include "emelfm2.h"
#include "e2_option.h"
#include "e2_output.h"
#include "e2_window.h"
#include <glib/gstdio.h>

static guint step;
static gint initial_mode;
static GString *message;

static void check_logs (gint mode)
{
    guint visible = 0;
    for (GList *link = app.tabslist; link != NULL; link = link->next)
    {
        E2_OutputTabRuntime *tab = link->data;
        if (!gtk_widget_get_mapped (tab->scroll)) continue;
        visible++;
        GtkScrolledWindow *scroll = GTK_SCROLLED_WINDOW (tab->scroll);
        GtkAdjustment *horizontal = gtk_scrolled_window_get_hadjustment (scroll);
        gdouble excess = gtk_adjustment_get_upper (horizontal) - gtk_adjustment_get_page_size (horizontal);
        GtkWidget *bar = gtk_scrolled_window_get_hscrollbar (scroll);
        fprintf (stderr, "wrap step %u, mode %d: horizontal excess %.1f, scrollbar %d\n",
            step, mode, excess, gtk_widget_get_mapped (bar));
        if (mode == 0)
        {
            g_assert_cmpfloat (excess, >, 1000);
            g_assert_true (gtk_widget_get_mapped (bar));
        }
        else
        {
            /* Hiding the scrollbar alone must not clip long unbroken text. */
            g_assert_cmpfloat (excess, <=, 1);
            g_assert_false (gtk_widget_get_mapped (bar));
            g_assert_cmpfloat (gtk_adjustment_get_value (horizontal), ==, 0);
        }
        GtkTextIter start, end;
        gtk_text_buffer_get_bounds (tab->buffer, &start, &end);
        gchar *text = gtk_text_buffer_get_text (tab->buffer, &start, &end, FALSE);
        g_assert_nonnull (strstr (text, message->str));
        g_free (text);
        g_assert_true (gtk_text_iter_forward_search (&start, "xxxxxxxx", 0, &start, &end, NULL));
        end = start;
        gtk_text_iter_forward_chars (&end, 4095);
        GdkRectangle first, last;
        gtk_text_view_get_iter_location (tab->text, &start, &first);
        gtk_text_view_get_iter_location (tab->text, &end, &last);
        GtkAllocation size;
        gtk_widget_get_allocation (GTK_WIDGET (tab->text), &size);
        g_assert_cmpint (size.width, <, 1000);
        fprintf (stderr, "text width %d, GTK wrap %d, first (%d,%d), last (%d,%d)\n",
            size.width, gtk_text_view_get_wrap_mode (tab->text), first.x, first.y, last.x, last.y);
        if (mode == 0) g_assert_cmpint (first.y, ==, last.y);
        else g_assert_cmpint (last.y, >, first.y);
    }
#ifdef E2_VTE
    g_assert_cmpuint (visible, ==, 2);
#else
    g_assert_cmpuint (visible, ==, 1);
#endif
}

static gboolean tick (gpointer unused)
{
    if (curr_view == NULL || curr_view->dir[0] == 0
        || g_atomic_int_get (&curr_view->listcontrols.cd_working)) return TRUE;
    CLOSEBGL
    switch (step)
    {
        case 0:
            initial_mode = e2_option_sel_get ("output-wrap-mode");
            e2_window_output_show (NULL, NULL);
            gtk_window_resize (GTK_WINDOW (app.main_window), 850, 600);
            gtk_paned_set_position (GTK_PANED (app.window.output_paned), 300);
            message = g_string_new (NULL);
            for (guint i = 0; i < 150; i++) g_string_append (message, "words with spaces 日本語 ");
            g_string_append_c (message, '\n');
            for (guint i = 0; i < 4096; i++) g_string_append_c (message, 'x');
            g_string_append (message, "\nend of long output\n");
            for (GList *link = app.tabslist; link != NULL; link = link->next)
                e2_output_print (link->data, message->str, "wrap-test", TRUE, "bold", NULL);
            break;
        case 1:
            check_logs (initial_mode);
            e2_option_sel_set ("output-wrap-mode", 0);
            break;
        case 2:
            check_logs (0);
            for (GList *link = app.tabslist; link != NULL; link = link->next)
                gtk_adjustment_set_value (gtk_scrolled_window_get_hadjustment (
                    GTK_SCROLLED_WINDOW (((E2_OutputTabRuntime *)link->data)->scroll)), 1000);
            e2_option_sel_set ("output-wrap-mode", 1);
            break;
        case 3:
            check_logs (1);
            gtk_window_resize (GTK_WINDOW (app.main_window), 650, 600);
            break;
        case 4:
            check_logs (1);
            e2_option_sel_set ("output-wrap-mode", 2);
            break;
        case 5:
            check_logs (2);
            e2_output_update_style ();
            break;
        case 6:
            check_logs (2);
            e2_window_recreate (&app.window);
            break;
        case 7:
        {
            check_logs (2);
            gchar *done = g_build_filename (g_getenv ("E2_OUTPUT_WRAP_TEST"), "passed", NULL);
            g_assert_true (g_file_set_contents (done, "passed", -1, NULL));
            g_free (done);
            g_string_free (message, TRUE);
            e2_main_closedown (TRUE, TRUE, TRUE);
            return FALSE;
        }
    }
    step++;
    OPENBGL
    return TRUE;
}
__attribute__((constructor)) static void schedule (void)
{ unsetenv ("LD_PRELOAD"); g_timeout_add (200, tick, NULL); }
