/* Exercise the real VTE controls in a private application profile. */
#include "emelfm2.h"
#include "e2_option.h"
#include "e2_terminal.h"
#include "e2_action.h"
#include "e2_pane.h"
#include "e2_command.h"
#include <gdk/gdkkeysyms.h>
#include <signal.h>
#include <errno.h>
#include <glib/gstdio.h>

static guint step;
static GtkWidget *book, *open_button;
static GtkWidget *left_book;
static GtkTextBuffer *left_buffer, *right_buffer;
static GtkWidget *main_book, *original_output;
static gint live_pid;
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
static gboolean open_output_menu (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    gchar *action = g_strconcat (_A(10), ".", _A(88), NULL);
    OPENBGL
    e2_action_run_simple_from (action, NULL, app.main_window);
    CLOSEBGL
    g_free (action);
    return TRUE;
}
static void check_output_menu (gboolean terminal)
{
    /* Dispatch with a real GTK event context, as keyboard-triggered menus do. */
    GtkWidget *trigger = gtk_invisible_new ();
    gtk_widget_show (trigger);
    g_signal_connect (trigger, "key-press-event", G_CALLBACK (open_output_menu), NULL);
    GdkEvent *event = gdk_event_new (GDK_KEY_PRESS);
    event->key.window = g_object_ref (gtk_widget_get_window (trigger));
    event->key.keyval = GDK_Menu;
    event->key.time = GDK_CURRENT_TIME;
#ifdef USE_GTK3_0
    GdkDeviceManager *manager = gdk_display_get_device_manager (gtk_widget_get_display (trigger));
    gdk_event_set_device (event, gdk_device_get_associated_device (gdk_device_manager_get_client_pointer (manager)));
#endif
    gtk_main_do_event (event);
    gdk_event_free (event);
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
    gtk_widget_destroy (trigger);
}
static gchar *buffer_text (GtkTextBuffer *buffer)
{
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds (buffer, &start, &end);
    return gtk_text_buffer_get_text (buffer, &start, &end, FALSE);
}
static void key (guint value)
{
    GdkEventKey event = {0};
    event.type = GDK_KEY_PRESS;
    event.keyval = value;
    event.state = GDK_CONTROL_MASK;
    event.window = gtk_widget_get_window (app.main_window);
    gboolean handled;
    g_signal_emit_by_name (app.main_window, "key-press-event", &event, &handled);
    g_assert_true (handled);
}
static gboolean main_page (gint index, gint count)
{
    return gtk_notebook_get_current_page (GTK_NOTEBOOK (main_book)) == index
        && gtk_notebook_get_n_pages (GTK_NOTEBOOK (main_book)) == count
        && gtk_widget_get_sensitive (app.window.panes_outer_box)
        && !app.pane1.view.listcontrols.newpath && !app.pane2.view.listcontrols.newpath;
}
static void close_main (gint index)
{
    GtkWidget *page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (main_book), index);
    GtkWidget *title = gtk_notebook_get_tab_label (GTK_NOTEBOOK (main_book), page);
    gtk_button_clicked (GTK_BUTTON (find_widget (title, NULL, GTK_TYPE_BUTTON)));
}
static gboolean answer_dialog (gpointer response)
{
    GList *windows = gtk_window_list_toplevels (), *link;
    gboolean answered = FALSE;
    for (link = windows; link != NULL; link = link->next)
        if (GTK_IS_MESSAGE_DIALOG (link->data) && gtk_widget_get_visible (link->data))
        {
            gtk_dialog_response (GTK_DIALOG (link->data), GPOINTER_TO_INT (response));
            answered = TRUE;
            break;
        }
    g_list_free (windows);
    return !answered;
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
                gchar *canonical = g_canonicalize_filename (contents, NULL);
                g_assert_cmpstr (canonical, ==, dir);
                g_free (canonical);
                g_free (contents); g_free (marker); g_free (dir);
            }
            e2_option_bool_set ("terminal-per-tab", TRUE);
            e2_window_recreate (&app.window);
            original_output = gtk_paned_get_child2 (GTK_PANED (app.window.output_paned));
            main_book = gtk_paned_get_child1 (GTK_PANED (app.window.output_paned));
            gchar *shell = g_build_filename (g_getenv ("E2_VTE_UI_TEST"), "live-shell", NULL);
            e2_option_str_set_direct (e2_option_get ("terminal-shell"), shell);
            g_free (shell);
            OPENBGL
            e2_action_run_simple_from ("terminal.open_here", NULL, app.main_window);
            CLOSEBGL
            gchar command[] = "sh -c 'sleep .5; printf %s%s TAB-ONE- ASYNC'";
            e2_command_run_at (command, curr_view->dir, E2_COMMAND_RANGE_DEFAULT, app.main_window);
            key (GDK_n);
            break;
        }
        case 4:
        {
            if (!main_page (1, 2)) goto wait;
            GtkWidget *output = gtk_paned_get_child2 (GTK_PANED (app.window.output_paned));
            g_assert_true (output != original_output);
            gchar *text = buffer_text (app.tab.buffer);
            g_assert_cmpstr (text, ==, "");
            g_free (text);
            e2_output_print (&app.tab, "TAB-TWO", NULL, TRUE, NULL);
            gchar *shell = g_build_filename (g_getenv ("E2_VTE_UI_TEST"), "shell", NULL);
            e2_option_str_set_direct (e2_option_get ("terminal-shell"), shell);
            g_free (shell);
            OPENBGL
            e2_action_run_simple_from ("terminal.open_here", NULL, app.main_window);
            CLOSEBGL
            book = find_widget (gtk_paned_get_child2 (GTK_PANED (output)), "terminal-notebook", 0);
            break;
        }
        case 5:
        {
            if (!exited (1)) goto wait;
            gchar *marker = g_build_filename (g_getenv ("E2_VTE_UI_TEST"), "right", "terminal-running", NULL), *pid;
            if (!g_file_get_contents (marker, &pid, NULL, NULL)) { g_free (marker); goto wait; }
            live_pid = atoi (pid);
            g_free (marker); g_free (pid);
            g_assert_cmpint (kill (live_pid, 0), ==, 0);
            gchar *text = buffer_text (right_buffer);
            gboolean complete = strstr (text, "TAB-ONE-ASYNC") != NULL;
            g_assert_null (strstr (text, "TAB-TWO"));
            g_free (text);
            if (!complete) goto wait;
            key (GDK_Tab);
            break;
        }
        case 6:
            if (!main_page (0, 2)) goto wait;
            g_assert_true (gtk_paned_get_child2 (GTK_PANED (app.window.output_paned)) == original_output);
            g_assert_true (app.tab.buffer == right_buffer);
            g_assert_cmpint (kill (live_pid, 0), ==, 0);
            e2_option_bool_set ("terminal-per-tab", FALSE);
            e2_window_recreate (&app.window);
            book = find_widget (gtk_paned_get_child2 (GTK_PANED (original_output)), "terminal-notebook", 0);
            g_assert_cmpint (gtk_notebook_get_n_pages (GTK_NOTEBOOK (book)), ==, 4);
            g_assert_cmpint (gtk_notebook_get_n_pages (GTK_NOTEBOOK (app.outbook)), ==, 2);
            key (GDK_Tab);
            break;
        case 7:
            if (!main_page (1, 2)) goto wait;
            g_assert_true (gtk_paned_get_child2 (GTK_PANED (app.window.output_paned)) == original_output);
            g_assert_cmpint (kill (live_pid, 0), ==, 0);
            e2_option_bool_set ("terminal-per-tab", TRUE);
            e2_window_recreate (&app.window);
            g_timeout_add (50, answer_dialog, GINT_TO_POINTER (GTK_RESPONSE_CANCEL));
            close_main (1);
            g_assert_true (main_page (1, 2));
            g_assert_cmpint (kill (live_pid, 0), ==, 0);
            g_timeout_add (50, answer_dialog, GINT_TO_POINTER (GTK_RESPONSE_ACCEPT));
            close_main (1);
            break;
        case 8:
        {
            if (!main_page (0, 1)) goto wait;
            if (kill (live_pid, 0) == 0) goto wait;
            g_assert_cmpint (errno, ==, ESRCH);
            e2_option_bool_set ("pane-tabs", FALSE);
            e2_window_recreate (&app.window);
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
