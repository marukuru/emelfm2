/* Exercise tabs through real GTK signals after asynchronous directory loading. */
#include "emelfm2.h"
#include "e2_option.h"
#include "e2_tabs.h"
#include "e2_pane.h"
#include "e2_action.h"
#include "e2_terminal.h"
#include <signal.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gstdio.h>

static GtkWidget *book, *output, *commandbar;
static guint step, first_history_length;
static const gchar *root;
static gboolean idle (void)
{
	return app.pane1.view.dir[0] && app.pane2.view.dir[0]
		&& !app.pane1.view.listcontrols.newpath && !app.pane2.view.listcontrols.newpath
		&& !g_atomic_int_get (&app.pane1.view.listcontrols.cd_working)
		&& !g_atomic_int_get (&app.pane2.view.listcontrols.cd_working)
		&& !g_atomic_int_get (&app.pane1.view.listcontrols.refresh_working)
		&& !g_atomic_int_get (&app.pane2.view.listcontrols.refresh_working)
		&& gtk_widget_get_sensitive (app.window.panes_outer_box);
}
static void path_is (E2_PaneRuntime *pane, const gchar *name)
{
	gchar *path = g_strconcat (root, "/", name, "/", NULL);
	g_assert_cmpstr (pane->view.dir, ==, path);
	g_free (path);
}
static void cd (E2_PaneRuntime *pane, const gchar *name)
{
	gchar *path = g_build_filename (root, name, NULL);
	e2_pane_change_dir (pane, path);
	g_free (path);
}
static void control_key (guint key)
{
	GdkEventKey event = {0};
	event.type = GDK_KEY_PRESS;
	event.keyval = key;
	event.state = GDK_CONTROL_MASK;
	event.window = gtk_widget_get_window (app.main_window);
	gboolean handled = FALSE;
	g_signal_emit_by_name (app.main_window, "key-press-event", &event, &handled);
	g_assert_true (handled);
}
static void new_tab (void)
{
	control_key (GDK_n);
}
static void next_tab (void)
{
	control_key (GDK_Tab);
}
static void focus_name (ViewInfo *view, const gchar *name)
{
	GtkTreeIter iter;
	g_assert_true (e2_tree_find_iter_from_str (view->model, FILENAME, name, &iter, FALSE));
	GtkTreePath *path = gtk_tree_model_get_path (view->model, &iter);
	gtk_tree_view_set_cursor (GTK_TREE_VIEW (view->treeview), path, NULL, FALSE);
	gtk_tree_path_free (path);
}
static void cursor_is (ViewInfo *view, const gchar *name)
{
	GtkTreePath *path;
	GtkTreeIter iter;
	gtk_tree_view_get_cursor (GTK_TREE_VIEW (view->treeview), &path, NULL);
	g_assert_nonnull (path);
	g_assert_true (gtk_tree_model_get_iter (view->model, &iter, path));
	gchar *actual;
	gtk_tree_model_get (view->model, &iter, FILENAME, &actual, -1);
	g_assert_cmpstr (actual, ==, name);
	g_free (actual);
	gtk_tree_path_free (path);
}
static gboolean page_is (gint number, gint count)
{
	return gtk_notebook_get_n_pages (GTK_NOTEBOOK (book)) == count
		&& gtk_notebook_get_current_page (GTK_NOTEBOOK (book)) == number && idle ();
}
static void structure (void)
{
	GtkWidget *page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (book),
		gtk_notebook_get_current_page (GTK_NOTEBOOK (book)));
	g_assert_true (gtk_widget_get_child_visible (page));
	g_assert_true (gtk_widget_get_mapped (app.pane1.view.treeview));
	g_assert_true (gtk_widget_get_mapped (app.pane2.view.treeview));
	g_assert_true (gtk_widget_is_ancestor (app.pane1.outer_box, page));
	g_assert_true (gtk_widget_is_ancestor (app.pane2.outer_box, page));
	g_assert_true (gtk_widget_is_ancestor (app.pane1.toolbar.toolbar_container, page));
	g_assert_true (gtk_widget_is_ancestor (app.pane2.toolbar.toolbar_container, page));
	g_assert_false (gtk_widget_is_ancestor (app.commandbar.toolbar_container, book));
	g_assert_false (gtk_widget_is_ancestor (app.outbook, book));
	g_assert_true (gtk_paned_get_child2 (GTK_PANED (app.window.output_paned)) == output);
}
static const gchar *label (gint index)
{
	GtkWidget *page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (book), index);
	GtkWidget *box = gtk_notebook_get_tab_label (GTK_NOTEBOOK (book), page);
	GList *children = gtk_container_get_children (GTK_CONTAINER (box));
	const gchar *text = gtk_label_get_text (GTK_LABEL (children->data));
	g_list_free (children);
	return text;
}
static void close_tab (gint index)
{
	GtkWidget *page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (book), index);
	GtkWidget *box = gtk_notebook_get_tab_label (GTK_NOTEBOOK (book), page);
	GList *children = gtk_container_get_children (GTK_CONTAINER (box));
	gtk_button_clicked (GTK_BUTTON (children->next->data));
	g_list_free (children);
}
static gboolean tick (gpointer data)
{
	if (app.main_window == NULL || curr_view == NULL || !idle ()) return TRUE;
	CLOSEBGL
	switch (step)
	{
		case 0:
			root = g_getenv ("E2_TABS_TEST");
			book = gtk_paned_get_child1 (GTK_PANED (app.window.output_paned));
			g_assert_true (GTK_IS_NOTEBOOK (book));
			output = gtk_paned_get_child2 (GTK_PANED (app.window.output_paned));
			commandbar = app.commandbar.toolbar_container;
			g_assert_true (page_is (0, 1));
			structure ();
			path_is (&app.pane1, "alpha"); path_is (&app.pane2, "beta");
			g_assert_cmpstr (label (0), ==, "alpha | beta");
			focus_name (&app.pane1.view, "two");
			GtkTreeIter selected;
			g_assert_true (e2_tree_find_iter_from_str (app.pane1.view.model,
				FILENAME, "one", &selected, FALSE));
			gtk_tree_selection_select_iter (app.pane1.view.selection, &selected);
			g_assert_cmpint (gtk_tree_selection_count_selected_rows (app.pane1.view.selection), ==, 2);
			focus_name (&app.pane2.view, "one");
			gtk_tree_selection_unselect_all (app.pane2.view.selection);
			first_history_length = g_list_length (app.pane1.opendirs);
#ifdef E2_VTE
			if (g_getenv ("E2_TABS_VTE") != NULL)
			{
				gchar *shell = g_build_filename (root, "shell", NULL);
				e2_option_str_set_direct (e2_option_get ("terminal-shell"), shell);
				g_free (shell);
				OPENBGL
				e2_action_run_simple_from ("terminal.open_here", NULL, app.main_window);
				CLOSEBGL
				g_assert_true (e2_terminal_has_focus ());
			}
#endif
			new_tab ();
			break;
		case 1:
			if (!page_is (1, 2)) goto wait;
			structure ();
			g_assert_true (app.commandbar.toolbar_container == commandbar);
			path_is (&app.pane1, "alpha"); path_is (&app.pane2, "beta");
			cd (&app.pane1, "gamma"); cd (&app.pane2, "delta");
			/* Start workers and rebuild while they need GTK to finish. */
			OPENBGL
			e2_fileview_cd_manage (&app.pane1.view.listcontrols);
			CLOSEBGL
			e2_window_recreate (&app.window);
			commandbar = app.commandbar.toolbar_container;
			/* The requested tab must retain its original pair of directories. */
			gtk_notebook_set_current_page (GTK_NOTEBOOK (book), 0);
			break;
		case 2:
			if (!page_is (0, 2)) goto wait;
			path_is (&app.pane1, "alpha"); path_is (&app.pane2, "beta");
			cursor_is (&app.pane1.view, "two");
			cursor_is (&app.pane2.view, "one");
			g_assert_true (gtk_window_get_focus (GTK_WINDOW (app.main_window)) == curr_view->treeview);
			g_assert_cmpint (gtk_tree_selection_count_selected_rows (app.pane1.view.selection), ==, 2);
			g_assert_cmpint (gtk_tree_selection_count_selected_rows (app.pane2.view.selection), ==, 0);
			g_assert_cmpstr (label (1), ==, "gamma | delta");
			g_assert_cmpuint (g_list_length (app.pane1.opendirs), ==, first_history_length);
			gtk_notebook_set_current_page (GTK_NOTEBOOK (book), 1);
			break;
		case 3:
			if (!page_is (1, 2)) goto wait;
			path_is (&app.pane1, "gamma"); path_is (&app.pane2, "delta");
			structure ();
			g_assert_true (app.commandbar.toolbar_container == commandbar);
			g_assert_cmpuint (g_list_length (app.pane1.opendirs), ==, first_history_length + 1);
			app.pane1.view.show_hidden = TRUE;
			e2_fileview_refilter_list (&app.pane1.view);
			e2_fileview_sort_column (SIZE, &app.pane1.view);
			focus_name (&app.pane1.view, "one");
			focus_name (&app.pane2.view, "two");
			gtk_notebook_set_current_page (GTK_NOTEBOOK (book), 0);
			break;
		case 4:
			if (!page_is (0, 2)) goto wait;
			g_assert_false (app.pane1.view.show_hidden);
			g_assert_cmpint (app.pane1.view.sort_column, ==, FILENAME);
			gtk_notebook_set_current_page (GTK_NOTEBOOK (book), 1);
			break;
		case 5:
			if (!page_is (1, 2)) goto wait;
			g_assert_true (app.pane1.view.show_hidden);
			g_assert_cmpint (app.pane1.view.sort_column, ==, SIZE);
			cursor_is (&app.pane1.view, "one");
			cursor_is (&app.pane2.view, "two");
			e2_option_bool_set ("panes-horizontal", TRUE);
			e2_window_recreate (&app.window);
			g_assert_true (page_is (1, 2));
			structure ();
			close_tab (0);
			g_assert_true (page_is (0, 1));
			new_tab (); new_tab (); new_tab ();
			break;
		case 6:
			if (!page_is (3, 4)) goto wait;
			path_is (&app.pane1, "gamma"); path_is (&app.pane2, "delta");
			close_tab (3);
			break;
		case 7:
			if (!page_is (2, 3)) goto wait;
			e2_option_bool_set ("pane-tabs", FALSE);
			e2_window_recreate (&app.window);
			g_assert_false (GTK_IS_NOTEBOOK (gtk_paned_get_child1 (GTK_PANED (app.window.output_paned))));
			path_is (&app.pane1, "gamma");
			e2_option_bool_set ("pane-tabs", TRUE);
			e2_window_recreate (&app.window);
			book = gtk_paned_get_child1 (GTK_PANED (app.window.output_paned));
			g_assert_cmpint (gtk_notebook_get_n_pages (GTK_NOTEBOOK (book)), ==, 1);
			structure ();
			new_tab ();
			break;
		case 8:
			if (!page_is (1, 2)) goto wait;
			structure ();
			path_is (&app.pane1, "gamma"); path_is (&app.pane2, "delta");
			gchar *literal = g_strconcat (root, "/$HOME %f 日本語/", NULL);
			e2_pane_restore_dir (&app.pane1, literal);
			g_free (literal);
			/* Also rebuild with a directory request queued but not dispatched. */
			e2_window_recreate (&app.window);
			gtk_notebook_set_current_page (GTK_NOTEBOOK (book), 0);
			break;
		case 9:
			if (!page_is (0, 2)) goto wait;
			path_is (&app.pane1, "gamma");
			g_assert_cmpstr (label (1), ==, "$HOME %f 日本語 | delta");
			gtk_notebook_set_current_page (GTK_NOTEBOOK (book), 1);
			break;
		case 10:
			if (!page_is (1, 2)) goto wait;
			path_is (&app.pane1, "$HOME %f 日本語");
			focus_name (&app.pane2.view, "two");
			if (curr_pane != &app.pane2) e2_pane_activate_other ();
			gtk_widget_grab_focus (app.pane2.view.treeview);
			next_tab ();
			break;
		case 11:
			if (!page_is (0, 2)) goto wait;
			path_is (&app.pane1, "gamma");
			g_assert_true (curr_pane == &app.pane1);
			next_tab ();
			break;
		case 12:
			if (!page_is (1, 2)) goto wait;
			path_is (&app.pane1, "$HOME %f 日本語");
			cursor_is (&app.pane2.view, "two");
			g_assert_true (curr_pane == &app.pane2);
			g_assert_true (gtk_window_get_focus (GTK_WINDOW (app.main_window)) == app.pane2.view.treeview);
			new_tab ();
			break;
		case 13:
			if (!page_is (2, 3)) goto wait;
			next_tab ();
			break;
		case 14:
			if (!page_is (0, 3)) goto wait;
			/* Both presses arrive before the deferred switch is processed. */
			next_tab (); next_tab ();
			break;
		case 15:
			if (!page_is (2, 3)) goto wait;
			close_tab (1); close_tab (0);
			next_tab ();
			break;
		case 16:
			if (!page_is (0, 1)) goto wait;
			cursor_is (&app.pane2.view, "two");
			structure ();
#ifdef E2_VTE
			if (g_getenv ("E2_TABS_VTE") != NULL)
			{
				gchar *marker = g_build_filename (root, "alpha", "terminal-started", NULL);
				gchar *pid;
				g_assert_true (g_file_get_contents (marker, &pid, NULL, NULL));
				g_assert_cmpint (kill (atoi (pid), 0), ==, 0);
				g_free (marker); g_free (pid);
			}
#endif
			gchar *done = g_build_filename (root, "passed", NULL);
			g_file_set_contents (done, "passed", -1, NULL);
			g_free (done);
			e2_main_closedown (TRUE, TRUE, TRUE);
			return FALSE;
	}
	fprintf (stderr, "pane-tabs: step %u passed\n", step++);
wait:
	OPENBGL
	return TRUE;
}
static gboolean timed_out (gpointer data)
{
	g_error ("tab test timed out at step %u: cd %d/%d refresh %d/%d paths %s | %s",
		step, app.pane1.view.listcontrols.cd_working, app.pane2.view.listcontrols.cd_working,
		app.pane1.view.listcontrols.refresh_working, app.pane2.view.listcontrols.refresh_working,
		app.pane1.view.dir, app.pane2.view.dir);
	return FALSE;
}
__attribute__((constructor)) static void schedule (void)
{
	unsetenv ("LD_PRELOAD");
	g_timeout_add (100, tick, NULL);
	g_timeout_add_seconds (15, timed_out, NULL);
}
