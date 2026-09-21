/* Exercise the real VTE controls in a private application profile. */
#include "emelfm2.h"
#include "e2_option.h"
#include "e2_terminal.h"
#include "e2_terminal_backend.h"
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
static gint live_pid, normal_height;
static gint stacked_files_position;
static GtkTreePath *saved_cursor;
static GtkWidget *search_entry, *activity_terminal;
static gint64 pause_until;
static GtkWidget *find_widget (GtkWidget *widget, const gchar *name, GType type)
{
    if ((name != NULL && !strcmp (gtk_widget_get_name (widget), name))
        || (type != 0 && G_TYPE_CHECK_INSTANCE_TYPE (widget, type))) return widget;
    if (GTK_IS_NOTEBOOK (widget))
    {
        GtkWidget *actions = gtk_notebook_get_action_widget (GTK_NOTEBOOK (widget), GTK_PACK_END);
        if (actions != NULL)
        {
            GtkWidget *found = find_widget (actions, name, type);
            if (found != NULL) return found;
        }
    }
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
    gchar *state = gtk_widget_get_tooltip_text (label);
    gboolean result = state != NULL && g_str_has_prefix (state, "exited\n");
    g_free (state);
    return result;
}
static gboolean open_output_menu (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    if (data != NULL)
    {
        gtk_button_clicked (GTK_BUTTON (data));
        return TRUE;
    }
    gchar *action = g_strconcat (_A(10), ".", _A(88), NULL);
    OPENBGL
    e2_action_run_simple_from (action, NULL, app.main_window);
    CLOSEBGL
    g_free (action);
    return TRUE;
}
static void check_output_menu (gboolean terminal, gboolean from_button)
{
    /* Dispatch with a real GTK event context, as keyboard-triggered menus do. */
    GtkWidget *trigger = gtk_invisible_new ();
    gtk_widget_show (trigger);
    GtkWidget *button = from_button ? find_widget (book, "terminal-menu", 0) : NULL;
    g_signal_connect (trigger, "key-press-event", G_CALLBACK (open_output_menu), button);
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
    if (from_button)
    {
        g_assert_true (gtk_menu_get_attach_widget (GTK_MENU (menu)) == button);
        gint bx, by, wx, wy, mx, my;
        GtkAllocation ba, ma;
        gtk_widget_get_allocation (button, &ba);
        gtk_widget_get_allocation (menu, &ma);
        gtk_widget_translate_coordinates (button, app.main_window, 0, 0, &bx, &by);
        gdk_window_get_origin (gtk_widget_get_window (app.main_window), &wx, &wy);
        gdk_window_get_origin (gtk_widget_get_window (menu), &mx, &my);
        bx += wx; by += wy;
        g_assert_cmpint (mx, <=, bx + ba.width);
        g_assert_cmpint (mx + ma.width, >=, bx);
        g_assert_true (ABS (my - by - ba.height) < 10 || ABS (my + ma.height - by) < 10);
    }
    GList *items = gtk_container_get_children (GTK_CONTAINER (menu)), *link;
    gboolean edit = FALSE;
    for (link = items; link != NULL; link = link->next)
    {
        GtkWidget *label = find_widget (link->data, NULL, GTK_TYPE_LABEL);
        if (label != NULL && !strcmp (gtk_label_get_text (GTK_LABEL (label)), "Edit")) edit = TRUE;
    }
    g_assert_cmpint (edit, ==, !terminal);
    if (terminal) g_assert_cmpuint (g_list_length (items), >=, 7);
    g_list_free (items);
    gtk_menu_popdown (GTK_MENU (menu));
    if (from_button) g_signal_emit_by_name (menu, "selection-done");
    gtk_widget_destroy (menu);
    gtk_widget_destroy (trigger);
}
static void equal_tools (GtkWidget *split)
{
    GdkEvent *event = gdk_event_new (GDK_BUTTON_PRESS);
    event->button.window = g_object_ref (gtk_paned_get_handle_window (GTK_PANED (split)));
    event->button.button = 3;
    event->button.time = GDK_CURRENT_TIME;
#ifdef USE_GTK3_0
    GdkDeviceManager *manager = gdk_display_get_device_manager (gtk_widget_get_display (split));
    gdk_event_set_device (event, gdk_device_manager_get_client_pointer (manager));
#endif
    gtk_main_do_event (event);
    gdk_event_free (event);
    GtkWidget *menu = gtk_grab_get_current ();
    g_assert_true (GTK_IS_MENU (menu));
    g_assert_true (gtk_menu_get_attach_widget (GTK_MENU (menu)) == split);
    GList *items = gtk_container_get_children (GTK_CONTAINER (menu));
    g_assert_cmpuint (g_list_length (items), ==, 1);
    GtkWidget *label = find_widget (items->data, NULL, GTK_TYPE_LABEL);
    g_assert_cmpstr (gtk_label_get_text (GTK_LABEL (label)), ==, "Equal panel sizes (1:1)");
    gtk_menu_item_activate (GTK_MENU_ITEM (items->data));
    g_list_free (items);
    gtk_menu_popdown (GTK_MENU (menu));
    g_signal_emit_by_name (menu, "selection-done");
}
static void check_equal_tools (GtkWidget *split)
{
    GtkAllocation left, right;
    gtk_widget_get_allocation (gtk_paned_get_child1 (GTK_PANED (split)), &left);
    gtk_widget_get_allocation (gtk_paned_get_child2 (GTK_PANED (split)), &right);
    g_assert_cmpint (ABS (left.width - right.width), <=, 1);
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
static void search_key (guint keyval, GdkModifierType state)
{
    GdkEventKey event = {0}; event.keyval = keyval; event.state = state;
    gboolean handled = FALSE;
    g_signal_emit_by_name (search_entry, "key-press-event", &event, &handled);
    g_assert_true (handled);
}
static const gchar *unread (gint page)
{
    GtkWidget *badge = find_widget (tab_title (page), "tools-unread", 0);
    return gtk_label_get_text (GTK_LABEL (badge));
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
            /* A legacy profile can contain several log pages. Consolidation
             * must preserve all text, including styled text, before removal. */
            GtkWidget *legacy = e2_output_create_notebook (3);
            g_object_ref_sink (legacy);
            for (gint i = 0; i < 3; i++)
            {
                GtkWidget *page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (legacy), i);
                E2_OutputTabRuntime *rt = g_object_get_data (G_OBJECT (page), "e2-output-tab");
                gchar *text = g_strdup_printf ("LEGACY-%d", i);
                gtk_text_buffer_set_text (rt->buffer, text, -1);
                GtkTextIter start, end;
                gtk_text_buffer_get_bounds (rt->buffer, &start, &end);
                gtk_text_buffer_apply_tag_by_name (rt->buffer, "bold", &start, &end);
                g_free (text);
            }
            e2_output_merge_notebooks (legacy, legacy);
            g_assert_cmpint (gtk_notebook_get_n_pages (GTK_NOTEBOOK (legacy)), ==, 1);
            GtkWidget *page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (legacy), 0);
            E2_OutputTabRuntime *rt = g_object_get_data (G_OBJECT (page), "e2-output-tab");
            gchar *merged = buffer_text (rt->buffer);
            g_assert_nonnull (strstr (merged, "LEGACY-0"));
            g_assert_nonnull (strstr (merged, "LEGACY-1"));
            g_assert_nonnull (strstr (merged, "LEGACY-2"));
            g_free (merged);
            e2_output_destroy_notebook (legacy, app.outbook);
            g_object_unref (legacy);
            const gchar *root = g_getenv ("E2_VTE_UI_TEST");
            gchar *shell = g_build_filename (root, "shell", NULL);
            e2_option_str_set_direct (e2_option_get ("terminal-shell"), shell);
            g_free (shell);
            book = find_widget (app.main_window, "terminal-notebook", 0);
            g_assert_nonnull (book);
            GtkWidget *bar = find_widget (app.main_window, "terminal-actions", 0);
            GList *buttons = gtk_container_get_children (GTK_CONTAINER (bar)), *link;
            g_assert_cmpuint (g_list_length (buttons), ==, 2);
            for (link = buttons; link != NULL; link = link->next)
            {
                g_assert_true (gtk_widget_get_has_tooltip (link->data));
                gchar *tip = gtk_widget_get_tooltip_text (link->data);
                g_assert_nonnull (tip);
                g_assert_true (*tip != '\0');
                g_free (tip);
                g_assert_nonnull (find_widget (link->data, NULL, GTK_TYPE_IMAGE));
            }
            open_button = buttons->data;
            g_list_free (buttons);
            gtk_button_clicked (GTK_BUTTON (open_button));
            break;
        }
        case 1:
        {
            if (!exited (1)) goto wait;
            GtkWidget *label = find_widget (tab_title (1), NULL, GTK_TYPE_LABEL);
            gchar *expected = g_strconcat (g_get_user_name (), "@left", NULL);
            g_assert_cmpstr (gtk_label_get_text (GTK_LABEL (label)), ==, expected);
            g_free (expected);
            gtk_button_clicked (GTK_BUTTON (open_button));
            break;
        }
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
            check_output_menu (TRUE, FALSE);
            check_output_menu (TRUE, TRUE);
            gtk_notebook_set_current_page (GTK_NOTEBOOK (book), 0);
            check_output_menu (FALSE, FALSE);
            check_output_menu (FALSE, TRUE);
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
            check_output_menu (TRUE, TRUE);
            gtk_notebook_set_current_page (GTK_NOTEBOOK (book), 0);
            check_output_menu (FALSE, TRUE);
            gtk_notebook_set_current_page (GTK_NOTEBOOK (book), 1);
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
                g_assert_cmpint (gtk_notebook_get_n_pages (GTK_NOTEBOOK (output_book)), ==, 2);
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
            gchar command[] = "sh -c 'sleep 1; printf %s%s MERGED- LATE'";
            e2_command_run_at (command, curr_view->dir, E2_COMMAND_RANGE_DEFAULT, app.main_window);
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
            g_assert_cmpint (gtk_notebook_get_n_pages (GTK_NOTEBOOK (app.outbook)), ==, 4);
            gchar *merged = buffer_text (right_buffer);
            g_assert_nonnull (strstr (merged, "TAB-TWO"));
            g_assert_nonnull (strstr (merged, "TAB-ONE-ASYNC"));
            g_free (merged);
            key (GDK_Tab);
            break;
        case 7:
        {
            if (!main_page (1, 2)) goto wait;
            gchar *text = buffer_text (right_buffer);
            gboolean complete = strstr (text, "MERGED-LATE") != NULL;
            g_free (text);
            if (!complete) goto wait;
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
        }
        case 8:
        {
            if (!main_page (0, 1)) goto wait;
            if (kill (live_pid, 0) == 0) goto wait;
            g_assert_cmpint (errno, ==, ESRCH);
            e2_option_bool_set ("pane-tabs", FALSE);
            e2_window_recreate (&app.window);
            book = app.outbook;
            e2_window_output_show (NULL, NULL);
            gtk_paned_set_position (GTK_PANED (app.window.panes_paned), 350);
            break;
        }
        case 9:
        {
            GtkWidget *root = gtk_paned_get_child2 (GTK_PANED (app.window.output_paned));
            gint x, y;
            gtk_widget_translate_coordinates (app.window.panes_paned, root,
                gtk_paned_get_position (GTK_PANED (app.window.panes_paned)), 0, &x, &y);
            g_assert_cmpint (abs (gtk_paned_get_position (GTK_PANED (root)) - x), <=, 2);
            /* Simulate a handle drag, not a layout-generated position change. */
            GdkEventButton drag = {0}; drag.button = 1;
            drag.type = GDK_BUTTON_PRESS;
            drag.window = gtk_paned_get_handle_window (GTK_PANED (root));
            gboolean handled;
            g_signal_emit_by_name (root, "button-press-event", &drag, &handled);
            gtk_paned_set_position (GTK_PANED (root), 440);
            g_signal_emit_by_name (root, "button-release-event", &drag, &handled);
            gchar *shell = g_build_filename (g_getenv ("E2_VTE_UI_TEST"), "folder-shell", NULL);
            e2_option_str_set_direct (e2_option_get ("terminal-shell"), shell);
            g_free (shell);
            OPENBGL
            e2_action_run_simple_from ("terminal.open_here", NULL, app.main_window);
            CLOSEBGL
            break;
        }
        case 10:
        {
            GtkWidget *label = find_widget (tab_title (1), NULL, GTK_TYPE_LABEL);
            if (!g_str_has_suffix (gtk_label_get_text (GTK_LABEL (label)), "@left")) goto wait;
            gchar *tip = gtk_widget_get_tooltip_text (label);
            g_assert_nonnull (strstr (tip, "/left"));
            g_free (tip);
            GtkWidget *root = gtk_paned_get_child2 (GTK_PANED (app.window.output_paned));
            gint x, y;
            gtk_widget_translate_coordinates (root, app.window.panes_paned,
                gtk_paned_get_position (GTK_PANED (root)), 0, &x, &y);
            g_assert_cmpint (abs (gtk_paned_get_position (GTK_PANED (app.window.panes_paned)) - x), <=, 2);
            OPENBGL
            e2_action_run_simple_from ("terminal.next_tab", NULL, app.main_window);
            e2_action_run_simple_from ("terminal.previous_tab", NULL, app.main_window);
            e2_action_run_simple_from ("terminal.show_folder", NULL, app.main_window);
            CLOSEBGL
            break;
        }
        case 11:
            if (!g_str_has_suffix (curr_view->dir, "/left/")) goto wait;
            g_assert_true (gtk_window_get_focus (GTK_WINDOW (app.main_window)) == curr_view->treeview);
            g_assert_cmpint (gtk_notebook_get_current_page (GTK_NOTEBOOK (book)), ==, 1);
            gtk_tree_view_get_cursor (GTK_TREE_VIEW (curr_view->treeview), &saved_cursor, NULL);
            normal_height = gtk_paned_get_position (GTK_PANED (app.window.output_paned));
            OPENBGL
            e2_action_run_simple_from ("terminal.expand_tools", NULL, app.main_window);
            CLOSEBGL
            break;
        case 12:
        {
            GtkWidget *root = gtk_paned_get_child2 (GTK_PANED (app.window.output_paned));
            g_assert_cmpint (gtk_paned_get_position (GTK_PANED (app.window.output_paned)), ==, 0);
            g_assert_false (gtk_widget_get_visible (curr_pane == &app.pane2 ?
                gtk_paned_get_child1 (GTK_PANED (root)) : gtk_paned_get_child2 (GTK_PANED (root))));
            OPENBGL
            e2_action_run_simple_from ("terminal.expand_tools", NULL, app.main_window);
            CLOSEBGL
            break;
        }
        case 13:
            g_assert_cmpint (abs (gtk_paned_get_position (GTK_PANED (app.window.output_paned)) - normal_height), <=, 3);
            OPENBGL
            e2_action_run_simple_from ("terminal.hide_tools", NULL, app.main_window);
            CLOSEBGL
            break;
        case 14:
        {
            g_assert_false (app.output.visible);
            g_assert_true (gtk_window_get_focus (GTK_WINDOW (app.main_window)) == curr_view->treeview);
            GtkTreePath *path;
            gtk_tree_view_get_cursor (GTK_TREE_VIEW (curr_view->treeview), &path, NULL);
            if (saved_cursor != NULL)
            {
                g_assert_cmpint (gtk_tree_path_compare (saved_cursor, path), ==, 0);
                gtk_tree_path_free (saved_cursor);
            }
            if (path != NULL) gtk_tree_path_free (path);
            e2_window_output_show (NULL, NULL);
            g_timeout_add (50, answer_dialog, GINT_TO_POINTER (GTK_RESPONSE_ACCEPT));
            OPENBGL
            e2_action_run_simple_from ("terminal.close", NULL, app.main_window);
            CLOSEBGL
            break;
        }
        case 15:
        {
            g_assert_cmpint (abs (gtk_paned_get_position (GTK_PANED (app.window.output_paned)) - normal_height), <=, 5);
            gchar *clear = g_strconcat (_A(10), ".", _A(36), NULL);
            OPENBGL
            e2_action_run_simple_from (clear, NULL, app.main_window);
            CLOSEBGL
            g_free (clear);
            gtk_text_buffer_set_text (app.tab.buffer, "One Needle.*日本語\nTwo Needle.*日本語\n", -1);
            OPENBGL
            e2_action_run_simple_from ("terminal.find", NULL, app.main_window);
            CLOSEBGL
            break;
        }
        case 16:
        {
            search_entry = find_widget (gtk_widget_get_parent (book), "tools-search-entry", 0);
            g_assert_true (gtk_window_get_focus (GTK_WINDOW (app.main_window)) == search_entry);
            gtk_entry_set_text (GTK_ENTRY (search_entry), "needle.*日本語");
            GtkTextIter a, b;
            g_assert_true (gtk_text_buffer_get_selection_bounds (app.tab.buffer, &a, &b));
            g_assert_cmpint (gtk_text_iter_get_offset (&a), ==, 4);
            g_signal_emit_by_name (search_entry, "activate");
            gtk_text_buffer_get_selection_bounds (app.tab.buffer, &a, &b);
            g_assert_cmpint (gtk_text_iter_get_offset (&a), >, 4);
            g_signal_emit_by_name (search_entry, "activate");
            gtk_text_buffer_get_selection_bounds (app.tab.buffer, &a, &b);
            g_assert_cmpint (gtk_text_iter_get_offset (&a), ==, 4);
            search_key (GDK_Return, GDK_SHIFT_MASK);
            gtk_text_buffer_get_selection_bounds (app.tab.buffer, &a, &b);
            g_assert_cmpint (gtk_text_iter_get_offset (&a), >, 4);
            gtk_entry_set_text (GTK_ENTRY (search_entry), "no such literal");
            GtkWidget *result = find_widget (gtk_widget_get_parent (book), "tools-search-result", 0);
            g_assert_cmpstr (gtk_label_get_text (GTK_LABEL (result)), ==, "No matches");
            search_key (GDK_Escape, 0);
            g_assert_true (gtk_window_get_focus (GTK_WINDOW (app.main_window)) == GTK_WIDGET (app.tab.text));
            gchar *shell = g_build_filename (g_getenv ("E2_VTE_UI_TEST"), "activity-shell", NULL);
            e2_option_str_set_direct (e2_option_get ("terminal-shell"), shell);
            g_free (shell);
            OPENBGL
            e2_action_run_simple_from ("terminal.open_here", NULL, app.main_window);
            CLOSEBGL
            break;
        }
        case 17:
        {
            activity_terminal = find_widget (gtk_notebook_get_nth_page (GTK_NOTEBOOK (book), 1), NULL, g_type_from_name ("VteTerminal"));
            gchar *text = e2_terminal_backend_text (activity_terminal);
            gboolean ready = text != NULL && strstr (text, "Ready") != NULL;
            g_free (text);
            if (!ready) goto wait;
            gtk_notebook_set_current_page (GTK_NOTEBOOK (book), 0);
            gchar *emit = g_build_filename (curr_view->dir, "emit-output", NULL);
            g_assert_true (g_file_set_contents (emit, "", 0, NULL));
            g_free (emit);
            break;
        }
        case 18:
            if (!*unread (1)) goto wait;
            gtk_window_resize (GTK_WINDOW (app.main_window), 850, 600);
            g_assert_cmpint (gtk_notebook_get_current_page (GTK_NOTEBOOK (book)), ==, 0);
            g_assert_true (gtk_window_get_focus (GTK_WINDOW (app.main_window)) == GTK_WIDGET (app.tab.text));
            gtk_notebook_set_current_page (GTK_NOTEBOOK (book), 1);
            e2_output_print (&app.tab, "BACKGROUND-LOG", NULL, FALSE, NULL);
            break;
        case 19:
            if (!*unread (0) || *unread (1)) goto wait;
            g_timeout_add (50, answer_dialog, GINT_TO_POINTER (GTK_RESPONSE_CANCEL));
            OPENBGL
            e2_action_run_simple_from ("terminal.close", NULL, app.main_window);
            CLOSEBGL
            g_assert_cmpint (gtk_notebook_get_n_pages (GTK_NOTEBOOK (book)), ==, 2);
            g_assert_true (gtk_window_get_focus (GTK_WINDOW (app.main_window)) == activity_terminal);
            OPENBGL
            e2_action_run_simple_from ("terminal.find", NULL, app.main_window);
            CLOSEBGL
            break;
        case 20:
            if (g_getenv ("E2_VTE_UI_SCREENSHOT") != NULL)
            {
                GtkAllocation a; gtk_widget_get_allocation (app.main_window, &a);
#ifdef USE_GTK3_0
                GdkPixbuf *shot = gdk_pixbuf_get_from_window (gtk_widget_get_window (app.main_window), 0, 0, a.width, a.height);
#else
                GdkPixbuf *shot = gdk_pixbuf_get_from_drawable (NULL, gtk_widget_get_window (app.main_window),
                    gtk_widget_get_colormap (app.main_window), 0, 0, 0, 0, a.width, a.height);
#endif
                g_assert_true (gdk_pixbuf_save (shot, g_getenv ("E2_VTE_UI_SCREENSHOT"), "png", NULL, NULL));
                g_object_unref (shot);
            }
            gtk_entry_set_text (GTK_ENTRY (search_entry), "UNREAD-TERMINAL");
            g_assert_true (e2_terminal_backend_has_selection (activity_terminal));
            search_key (GDK_Escape, 0);
            g_assert_true (gtk_window_get_focus (GTK_WINDOW (app.main_window)) == activity_terminal);
            OPENBGL
            e2_action_run_simple_from ("terminal.hide_tools", NULL, app.main_window);
            CLOSEBGL
            pause_until = g_get_monotonic_time () + 700000;
            break;
        case 21:
            if (g_get_monotonic_time () < pause_until) goto wait;
            e2_option_bool_set ("show-output-window-on-output", TRUE);
            e2_output_print (&app.tab, "STAY-HIDDEN", NULL, FALSE, NULL);
            pause_until = g_get_monotonic_time () + 700000;
            break;
        case 22:
        {
            if (g_get_monotonic_time () < pause_until) goto wait;
            g_assert_false (app.output.visible);
            g_assert_true (gtk_window_get_focus (GTK_WINDOW (app.main_window)) == curr_view->treeview);
            e2_option_bool_set ("pane-tabs", TRUE);
            e2_option_bool_set ("terminal-per-tab", TRUE);
            e2_window_recreate (&app.window);
            main_book = gtk_paned_get_child1 (GTK_PANED (app.window.output_paned));
            gchar command[] = "sh -c 'sleep .8; printf %s%s MAIN- HIDDEN'";
            e2_command_run_at (command, curr_view->dir, E2_COMMAND_RANGE_DEFAULT, app.main_window);
            key (GDK_n);
            break;
        }
        case 23:
        {
            if (!main_page (1, 2)) goto wait;
            GtkWidget *page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (main_book), 0);
            GtkWidget *label = gtk_notebook_get_tab_label (GTK_NOTEBOOK (main_book), page);
            GtkWidget *badge = find_widget (label, "main-tab-unread", 0);
            if (!*gtk_label_get_text (GTK_LABEL (badge))) goto wait;
            g_assert_true (gtk_window_get_focus (GTK_WINDOW (app.main_window)) == curr_view->treeview);
            key (GDK_Tab);
            break;
        }
        case 24:
            if (!main_page (0, 2)) goto wait;
            e2_terminal_select_output (book);
            gtk_notebook_set_current_page (GTK_NOTEBOOK (book), 1);
            g_timeout_add (50, answer_dialog, GINT_TO_POINTER (GTK_RESPONSE_ACCEPT));
            OPENBGL
            e2_action_run_simple_from ("terminal.close", NULL, app.main_window);
            CLOSEBGL
            e2_window_output_show (NULL, NULL);
            gtk_notebook_set_current_page (GTK_NOTEBOOK (book), 0);
            pause_until = g_get_monotonic_time () + 700000;
            break;
        case 25:
        {
            if (g_get_monotonic_time () < pause_until) goto wait;
            GtkWidget *page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (main_book), 0);
            GtkWidget *label = gtk_notebook_get_tab_label (GTK_NOTEBOOK (main_book), page);
            GtkWidget *badge = find_widget (label, "main-tab-unread", 0);
            g_assert_cmpstr (gtk_label_get_text (GTK_LABEL (badge)), ==, "");
            book = app.outbook;
            g_assert_cmpint (gtk_notebook_get_n_pages (GTK_NOTEBOOK (book)), ==, 1);
            gchar *shell = g_build_filename (g_getenv ("E2_VTE_UI_TEST"), "bash", NULL);
            e2_option_str_set_direct (e2_option_get ("terminal-shell"), shell);
            e2_option_bool_set ("terminal-shell-integration", TRUE);
            g_free (shell);
            OPENBGL
            e2_action_run_simple_from ("terminal.open_here", NULL, app.main_window);
            CLOSEBGL
            break;
        }
        case 26:
        {
            if (!exited (1))
            {
                if (g_get_monotonic_time () > pause_until + 4000000)
                {
                    GtkWidget *label = find_widget (tab_title (1), NULL, GTK_TYPE_LABEL);
                    gchar *tip = gtk_widget_get_tooltip_text (label);
                    GtkWidget *term = find_widget (gtk_notebook_get_nth_page (GTK_NOTEBOOK (book), 1), NULL, g_type_from_name ("VteTerminal"));
                    gchar *text = e2_terminal_backend_text (term);
                    g_error ("integration session failed to exit: %s; %s", tip, text);
                }
                goto wait;
            }
            gchar *file = g_build_filename (curr_view->dir, "shell-args", NULL), *args;
            g_assert_true (g_file_get_contents (file, &args, NULL, NULL));
            gchar *prefix = g_strconcat ("--rcfile\n", g_get_tmp_dir (), "/emelfm2-bash-", NULL);
            g_assert_true (g_str_has_prefix (args, prefix));
            g_free (prefix);
            g_assert_true (g_str_has_suffix (args, "\n-i\n"));
            g_free (args); g_free (file);
            file = g_build_filename (curr_view->dir, "rc-path", NULL);
            gchar *rc;
            g_assert_true (g_file_get_contents (file, &rc, NULL, NULL));
            g_assert_true (g_file_test (rc, G_FILE_TEST_IS_REGULAR));
            GtkWidget *close = find_widget (tab_title (1), "terminal-close", 0);
            gtk_button_clicked (GTK_BUTTON (close));
            g_assert_false (g_file_test (rc, G_FILE_TEST_EXISTS));
            g_free (rc); g_free (file);
            e2_option_str_set_direct (e2_option_get ("terminal-shell"), "/bin/sh");
            OPENBGL
            e2_action_run_simple_from ("terminal.open_here", NULL, app.main_window);
            CLOSEBGL
            break;
        }
        case 27:
            activity_terminal = find_widget (gtk_notebook_get_nth_page (GTK_NOTEBOOK (book), 1), NULL,
                g_type_from_name ("VteTerminal"));
            if (e2_terminal_backend_foreground_pid (activity_terminal) <= 0) goto wait;
            e2_terminal_backend_send (activity_terminal, "printf 'IDLE-%s\\n' READY\n");
            break;
        case 28:
        {
            gchar *text = e2_terminal_backend_text (activity_terminal);
            gboolean ready = text != NULL && strstr (text, "IDLE-READY") != NULL;
            g_free (text);
            if (!ready) goto wait;
            /* Neither quitting nor closing an idle shell should open a dialog. */
            g_assert_true (e2_terminal_confirm_shutdown ());
            gtk_button_clicked (GTK_BUTTON (find_widget (tab_title (1), "terminal-close", 0)));
            g_assert_cmpint (gtk_notebook_get_n_pages (GTK_NOTEBOOK (book)), ==, 1);
            break;
        }
        case 29:
        {
            GtkWidget *split = find_widget (app.main_window, "terminal-split", 0);
            /* A bubbled right-click from a child must not open the divider menu. */
            GdkEventButton child = {0}; child.type = GDK_BUTTON_PRESS; child.button = 3;
            child.window = gtk_widget_get_window (split);
            gboolean handled = FALSE;
            g_signal_emit_by_name (split, "button-press-event", &child, &handled);
            g_assert_false (handled);
            equal_tools (split);
            break;
        }
        case 30:
        {
            GtkWidget *split = find_widget (app.main_window, "terminal-split", 0);
            check_equal_tools (split);
            gint x, y;
            gtk_widget_translate_coordinates (split, app.window.panes_paned,
                gtk_paned_get_position (GTK_PANED (split)), 0, &x, &y);
            g_assert_cmpint (ABS (gtk_paned_get_position (GTK_PANED (app.window.panes_paned)) - x), <=, 1);
            e2_option_bool_set ("panes-horizontal", TRUE);
            e2_window_recreate (&app.window);
            break;
        }
        case 31:
        {
            g_assert_true (app.window.panes_horizontal);
            GtkWidget *split = find_widget (app.main_window, "terminal-split", 0);
            gtk_paned_set_position (GTK_PANED (split), 200);
            stacked_files_position = gtk_paned_get_position (GTK_PANED (app.window.panes_paned));
            equal_tools (split);
            break;
        }
        case 32:
        {
            check_equal_tools (find_widget (app.main_window, "terminal-split", 0));
            g_assert_cmpint (gtk_paned_get_position (GTK_PANED (app.window.panes_paned)), ==, stacked_files_position);
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
