/* Exercise the real VTE controls in a private application profile. */
#include "emelfm2.h"
#include "e2_option.h"
#include "e2_terminal.h"
#include "e2_action.h"
#include "e2_pane.h"
#include "e2_command.h"
#include <glib/gstdio.h>

static guint step;
static GtkWidget *book, *open_button;
static GtkWidget *left_book;
static GtkTextBuffer *left_buffer, *right_buffer;
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
static void check_output_menu (gboolean terminal)
{
    gchar *action = g_strconcat (_A(10), ".", _A(88), NULL);
    OPENBGL
    e2_action_run_simple_from (action, NULL, app.main_window);
    CLOSEBGL
    g_free (action);
    GtkWidget *menu = gtk_grab_get_current ();
    g_assert_true (GTK_IS_MENU (menu));
    GList *items = gtk_container_get_children (GTK_CONTAINER (menu)), *link;
    gboolean edit = FALSE;
    for (link = items; link != NULL; link = link->next)
    {
        GtkWidget *label = find_widget (link->data, NULL, GTK_TYPE_LABEL);
        if (label != NULL && !strcmp (gtk_label_get_text (GTK_LABEL (label)), "Edit")) edit = TRUE;
    }
    g_assert_cmpint (edit, ==, !terminal);
    if (terminal) g_assert_cmpuint (g_list_length (items), ==, 2);
    g_list_free (items);
    gtk_menu_popdown (GTK_MENU (menu));
    gtk_widget_destroy (menu);
}
static gchar *buffer_text (GtkTextBuffer *buffer)
{
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds (buffer, &start, &end);
    return gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
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
            check_output_menu (TRUE);
            gtk_notebook_set_current_page (GTK_NOTEBOOK (book), 0);
            check_output_menu (FALSE);
            left_book = book;
            left_buffer = app.tab.buffer;
            e2_output_print (&app.tab, "LEFT-ONLY", NULL, TRUE, NULL);
            gchar command[] = "sh -c 'sleep .3; printf %s%s LEFT- DELAYED'";
            e2_command_run_at (command, app.pane1.view.dir, E2_COMMAND_RANGE_DEFAULT, app.main_window);
            e2_pane_activate_other ();
            right_buffer = app.tab.buffer;
            g_assert_true (left_buffer != right_buffer);
            e2_output_print (&app.tab, "RIGHT-ONLY", NULL, TRUE, NULL);
            GtkWidget *split = find_widget (app.main_window, "terminal-split", 0);
            g_assert_true (GTK_IS_HPANED (split));
            GtkWidget *right = gtk_paned_get_child2 (GTK_PANED (split));
            book = find_widget (right, "terminal-notebook", 0);
            g_assert_true (book != left_book);
            GtkWidget *bar = find_widget (right, "terminal-actions", 0);
            GList *buttons = gtk_container_get_children (GTK_CONTAINER (bar));
            gtk_button_clicked (GTK_BUTTON (buttons->data));
            g_list_free (buttons);
            break;
        }
        case 3:
        {
            if (!exited (1)) goto wait;
            gchar *left = buffer_text (left_buffer);
            gboolean complete = strstr (left, "LEFT-DELAYED") != NULL;
            g_assert_nonnull (strstr (left, "LEFT-ONLY"));
            g_assert_null (strstr (left, "RIGHT-ONLY"));
            g_free (left);
            if (!complete) goto wait;
            gchar *right = buffer_text (right_buffer);
            g_assert_nonnull (strstr (right, "RIGHT-ONLY"));
            g_assert_null (strstr (right, "LEFT-DELAYED"));
            g_free (right);
            GtkWidget *output_book = app.outbook;
            for (guint i = 0; i < 2; i++)
            {
                gchar *action = g_strconcat (_A(10), ".", i ? _A(45) : _A(31), NULL);
                OPENBGL
                e2_action_run_simple_from (action, NULL, app.main_window);
                CLOSEBGL
                g_free (action);
                g_assert_true (app.outbook == output_book);
                g_assert_cmpint (gtk_notebook_get_n_pages (GTK_NOTEBOOK (output_book)), ==, i ? 1 : 2);
            }
            g_assert_true (app.tab.buffer == right_buffer);
            for (guint i = 0; i < 2; i++)
            {
                gchar *dir = g_build_filename (g_getenv ("E2_VTE_UI_TEST"), i ? "right" : "left", NULL);
                gchar *marker = g_build_filename (dir, "terminal-started", NULL), *contents;
                g_assert_true (g_file_get_contents (marker, &contents, NULL, NULL));
                g_strchomp (contents);
                g_assert_cmpstr (contents, ==, dir);
                g_free (contents); g_free (marker); g_free (dir);
            }
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
