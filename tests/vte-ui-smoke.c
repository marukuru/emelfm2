/* Exercise the real VTE controls in a private application profile. */
#include "emelfm2.h"
#include "e2_option.h"
#include "e2_terminal.h"
#include <glib/gstdio.h>

static guint step;
static GtkWidget *book, *open_button;
static GtkWidget *find_widget (GtkWidget *widget, const gchar *name, GType type)
{
    if ((name != NULL && !strcmp (gtk_widget_get_name (widget), name))
        || (type != 0 && G_TYPE_CHECK_INSTANCE_TYPE (widget, type))) return widget;
    if (!GTK_IS_CONTAINER (widget)) return NULL;
    GList *children = gtk_container_get_children (GTK_CONTAINER (widget)), *link;
    GtkWidget *found = NULL;
    for (link = children; link != NULL && found == NULL; link = link->next)
        found = find_widget (link->data, name, type);
    g_list_free (children);
    return found;
}
static GtkWidget *tab_title (gint index)
{
    GtkWidget *page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (book), index);
    return gtk_notebook_get_tab_label (GTK_NOTEBOOK (book), page);
}
static gboolean exited (gint index)
{
    GtkWidget *label = find_widget (tab_title (index), NULL, GTK_TYPE_LABEL);
    return strstr (gtk_label_get_text (GTK_LABEL (label)), "exited") != NULL;
}
static gboolean tick (gpointer data)
{
    if (app.main_window == NULL || curr_view == NULL || !curr_view->dir[0]
        || app.pane1.view.listcontrols.cd_working || app.pane2.view.listcontrols.cd_working) return TRUE;
    CLOSEBGL
    switch (step)
    {
        case 0:
        {
            const gchar *root = g_getenv ("E2_VTE_UI_TEST");
            gchar *shell = g_build_filename (root, "shell", NULL);
            e2_option_str_set_direct (e2_option_get ("terminal-shell"), shell);
            g_free (shell);
            book = find_widget (app.main_window, "terminal-notebook", 0);
            g_assert_nonnull (book);
            GtkWidget *bar = find_widget (app.main_window, "terminal-actions", 0);
            GList *buttons = gtk_container_get_children (GTK_CONTAINER (bar)), *link;
            g_assert_cmpuint (g_list_length (buttons), ==, 6);
            for (link = buttons; link != NULL; link = link->next)
            {
                g_assert_true (gtk_widget_get_has_tooltip (link->data));
                g_assert_nonnull (find_widget (link->data, NULL, GTK_TYPE_IMAGE));
            }
            open_button = buttons->data;
            g_list_free (buttons);
            gtk_button_clicked (GTK_BUTTON (open_button));
            break;
        }
        case 1:
            if (!exited (1)) goto wait;
            gtk_button_clicked (GTK_BUTTON (open_button));
            break;
        case 2:
        {
            if (!exited (2)) goto wait;
            GtkWidget *second = gtk_notebook_get_nth_page (GTK_NOTEBOOK (book), 2);
            GtkWidget *close = find_widget (tab_title (1), "terminal-close", 0);
            g_assert_nonnull (close);
            gtk_button_clicked (GTK_BUTTON (close));
            g_assert_cmpint (gtk_notebook_get_n_pages (GTK_NOTEBOOK (book)), ==, 2);
            g_assert_true (gtk_notebook_get_nth_page (GTK_NOTEBOOK (book), 1) == second);
            g_assert_cmpint (gtk_notebook_get_current_page (GTK_NOTEBOOK (book)), ==, 1);
            gchar *done = g_build_filename (g_getenv ("E2_VTE_UI_TEST"), "passed", NULL);
            g_file_set_contents (done, "passed", -1, NULL);
            g_free (done);
            e2_main_closedown (TRUE, TRUE, TRUE);
            return FALSE;
        }
    }
    fprintf (stderr, "vte-ui: step %u passed\n", step++);
wait:
    OPENBGL
    return TRUE;
}
__attribute__((constructor)) static void schedule (void)
{
    unsetenv ("LD_PRELOAD");
    g_timeout_add (100, tick, NULL);
}
