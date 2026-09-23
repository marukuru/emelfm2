/* Exercise action search against the real GTK application and dispatch path. */
#include "emelfm2.h"
#include "e2_action.h"
#include "e2_keybinding.h"
#include "e2_option.h"
#include "e2_pane.h"
#include "e2_terminal.h"
#include "e2_command_line.h"
#include <gdk/gdkkeysyms.h>
#include <glib/gstdio.h>

static GtkWidget *dialog, *entry, *tree;
static guint step, dispatched;
static const gchar *root;

static GtkWidget *find (GtkWidget *widget, const gchar *name)
{
	if (!g_strcmp0 (gtk_widget_get_name (widget), name)) return widget;
	if (!GTK_IS_CONTAINER (widget)) return NULL;
	GList *children = gtk_container_get_children (GTK_CONTAINER (widget));
	GtkWidget *found = NULL;
	for (GList *p = children; p != NULL && found == NULL; p = p->next)
		found = find (p->data, name);
	g_list_free (children);
	return found;
}

static GtkWidget *popup (void)
{
	GList *windows = gtk_window_list_toplevels ();
	GtkWidget *found = NULL;
	for (GList *p = windows; p != NULL; p = p->next)
		if (!g_strcmp0 (gtk_widget_get_name (p->data), "action-search")) found = p->data;
	g_list_free (windows);
	return found;
}

static gboolean key (GtkWidget *widget, guint keyval, GdkModifierType state)
{
	GdkEventKey event = {0};
	event.type = GDK_KEY_PRESS;
	event.keyval = keyval;
	event.state = state;
	event.window = gtk_widget_get_window (widget);
	gboolean handled = FALSE;
	g_signal_emit_by_name (widget, "key-press-event", &event, &handled);
	return handled;
}

static void open_search (void)
{
	g_assert_true (key (app.main_window, GDK_A, GDK_CONTROL_MASK | GDK_SHIFT_MASK));
	dialog = popup ();
	g_assert_nonnull (dialog);
	entry = find (dialog, "action-search-entry");
	tree = find (dialog, "action-search-results");
	g_assert_true (gtk_window_get_focus (GTK_WINDOW (dialog)) == entry);
}

static gboolean row (const gchar *label, const gchar *shortcut, gboolean enabled)
{
	GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (tree));
	GtkTreeIter iter;
	if (!gtk_tree_model_get_iter_first (model, &iter)) return FALSE;
	do
	{
		gchar *text, *binding;
		gboolean available;
		gtk_tree_model_get (model, &iter, 0, &text, 2, &binding, 3, &available, -1);
		gboolean match = !strcmp (label, text);
		if (match)
		{
			if (shortcut != NULL) g_assert_cmpstr (binding, ==, shortcut);
			g_assert_cmpint (available, ==, enabled);
			GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
			gtk_tree_view_set_cursor (GTK_TREE_VIEW (tree), path, NULL, FALSE);
			gtk_tree_path_free (path);
		}
		g_free (text); g_free (binding);
		if (match) return TRUE;
	} while (gtk_tree_model_iter_next (model, &iter));
	return FALSE;
}

static void select_file (void)
{
	GtkTreeIter iter;
	g_assert_true (e2_tree_find_iter_from_str (curr_view->model, FILENAME, "one.txt", &iter, FALSE));
	GtkTreePath *path = gtk_tree_model_get_path (curr_view->model, &iter);
	gtk_tree_view_set_cursor (GTK_TREE_VIEW (curr_view->treeview), path, NULL, FALSE);
	gtk_tree_path_free (path);
}

static gboolean configured_action (gpointer from, E2_ActionRuntime *art)
{
	g_assert_cmpstr (art->data, ==, "日本語 argument");
	g_assert_true (from == curr_view->treeview);
	dispatched++;
	return TRUE;
}

static void add_binding (GtkTreeIter *parent, const gchar *keyname,
	const gchar *action, const gchar *argument, gboolean cont)
{
	GtkTreeStore *store = GTK_TREE_STORE (e2_option_get ("keybindings")->ex.tree.model);
	GtkTreeIter iter;
	gtk_tree_store_prepend (store, &iter, parent);
	gtk_tree_store_set (store, &iter, 0, "", 1, keyname, 2, cont,
		3, action, 4, argument, -1);
}

static void finish (void)
{
	gchar *marker = g_build_filename (root, "passed", NULL);
	g_file_set_contents (marker, "passed", -1, NULL);
	g_free (marker);
	OPENBGL
	e2_action_run_simple_from ("command.quit", NULL, app.main_window);
}

static void screenshot (GtkWidget *widget, const gchar *suffix)
{
	const gchar *prefix = g_getenv ("E2_SEARCH_SCREENSHOT");
	if (prefix == NULL) return;
	GtkAllocation allocation;
	gtk_widget_get_allocation (widget, &allocation);
#ifdef USE_GTK3_0
	GdkPixbuf *pixels = gdk_pixbuf_get_from_window (gtk_widget_get_window (widget),
		0, 0, allocation.width, allocation.height);
#else
	GdkPixbuf *pixels = gdk_pixbuf_get_from_drawable (NULL, gtk_widget_get_window (widget),
		NULL, 0, 0, 0, 0, allocation.width, allocation.height);
#endif
	gchar *path = g_strconcat (prefix, suffix, ".png", NULL);
	gdk_pixbuf_save (pixels, path, "png", NULL, NULL);
	g_object_unref (pixels);
	g_free (path);
}

static gboolean tick (gpointer data)
{
	if (app.main_window == NULL || curr_view == NULL || !app.pane1.view.dir[0]
		|| !app.pane2.view.dir[0] || g_atomic_int_get (&app.pane1.view.listcontrols.cd_working)
		|| g_atomic_int_get (&app.pane2.view.listcontrols.cd_working)) return TRUE;
	CLOSEBGL
	switch (step)
	{
		case 0:
		{
			root = g_getenv ("E2_SEARCH_TEST");
			const gchar *scenario = g_getenv ("E2_SEARCH_CASE");
			if (scenario != NULL)
			{
				E2_PaneRuntime *before = curr_pane;
				gboolean handled = key (app.main_window, GDK_A, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
				if (!strcmp (scenario, "upgraded"))
				{
					g_assert_true (handled);
					g_assert_nonnull (popup ());
					key (popup (), GDK_Escape, 0);
					finish ();
					return FALSE;
				}
				g_assert_null (popup ());
				if (!strcmp (scenario, "conflict"))
				{
					g_assert_true (handled);
					g_assert_true (curr_pane != before);
				}
				else g_assert_false (handled);
				finish ();
				return FALSE;
			}
			gtk_widget_grab_focus (curr_view->treeview);
			select_file ();
			screenshot (app.main_window, "-panes");
			open_search ();
			gtk_entry_set_text (GTK_ENTRY (entry), "CoPy");
			g_assert_true (row ("Copy to other pane", "F5", TRUE));
			g_assert_true (row ("Copy as…", "Shift+F5", TRUE));
			g_assert_true (row ("Copy with time", "Ctrl+F5", TRUE));
			g_assert_true (row ("Copy and merge", "Alt+F5", TRUE));
			row ("Copy to other pane", NULL, TRUE);
			g_assert_true (key (dialog, GDK_Down, 0));
			GtkTreePath *path;
			gtk_tree_view_get_cursor (GTK_TREE_VIEW (tree), &path, NULL);
			g_assert_cmpint (*gtk_tree_path_get_indices (path), ==, 1);
			gtk_tree_path_free (path);
			g_assert_true (key (dialog, GDK_Up, 0));
			break;
		}
		case 1:
			screenshot (dialog, "-popup");
			g_signal_emit_by_name (entry, "activate");
			g_assert_null (popup ());
			g_assert_true (gtk_window_get_focus (GTK_WINDOW (app.main_window)) == curr_view->treeview);
			break;
		case 2:
		{
			gchar *copied = g_build_filename (root, "destination", "one.txt", NULL);
			gboolean exists = g_file_test (copied, G_FILE_TEST_EXISTS);
			g_free (copied);
			if (!exists) { OPENBGL return TRUE; }
			g_assert_cmpint (gtk_tree_selection_count_selected_rows (curr_view->selection), ==, 1);
			open_search ();
			gtk_entry_set_text (GTK_ENTRY (entry), "this-is-not-a-shell-command; touch marker");
			g_assert_cmpint (gtk_tree_model_iter_n_children (gtk_tree_view_get_model (GTK_TREE_VIEW (tree)), NULL), ==, 0);
			g_signal_emit_by_name (entry, "activate");
			g_assert_nonnull (popup ());
			g_assert_true (key (dialog, GDK_Escape, 0));
			gtk_tree_selection_unselect_all (curr_view->selection);
			open_search ();
			gtk_entry_set_text (GTK_ENTRY (entry), "copy");
			g_assert_true (row ("Copy to other pane", "F5", FALSE));
			g_signal_emit_by_name (entry, "activate");
			g_assert_nonnull (popup ());
			gtk_entry_set_text (GTK_ENTRY (entry), "mkdir");
			g_assert_true (row ("New folder…", "F7", TRUE));
			gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_CLOSE);
			/* Cancellation restores a text entry, including its text/selection. */
			GtkWidget *path_entry = NULL;
			for (GList *p = app.command_lines; p != NULL; p = p->next)
			{
				E2_CommandLineRuntime *line = p->data;
				if (line->pane == curr_pane) path_entry = gtk_bin_get_child (GTK_BIN (line->combo));
			}
			g_assert_nonnull (path_entry);
			gtk_widget_grab_focus (path_entry);
			open_search ();
			key (dialog, GDK_Escape, 0);
			g_assert_true (gtk_window_get_focus (GTK_WINDOW (app.main_window)) == path_entry);
			gtk_widget_grab_focus (curr_view->treeview);
			/* Runtime registration and configured arguments are discovered afresh. */
			E2_Action action = {g_strdup ("test.unicode_action"), configured_action, TRUE, E2_ACTION_TYPE_ITEM, 0, NULL, NULL};
			e2_action_register (&action);
			GtkTreeModel *model = e2_option_get ("keybindings")->ex.tree.model;
			GtkTreeIter main, files;
			e2_tree_get_lowest_iter_for_str (model, 0, &main, "general.main");
			e2_tree_get_lowest_iter_for_str (model, 0, &files, "general.main.panes");
			add_binding (&main, "<Control>j", "test.unicode_action", "日本語 argument", FALSE);
			/* Main handlers have priority over GTK's focused-child propagation. */
			add_binding (&main, "F5", "file.move", "", FALSE);
			add_binding (&files, "F5", "file.copy", "", FALSE);
			/* A Continue binding must not advertise just one of several actions. */
			add_binding (&main, "<Control>k", "file.move", "", FALSE);
			add_binding (&main, "<Control>k", "file.copy", "", TRUE);
			e2_keybinding_clean ();
			e2_keybinding_register_all ();
			break;
		}
		case 3:
			select_file ();
			open_search ();
			gtk_entry_set_text (GTK_ENTRY (entry), "copy");
			g_assert_true (row ("Copy to other pane", "", TRUE));
			gtk_entry_set_text (GTK_ENTRY (entry), "move");
			g_assert_true (row ("Move to other pane", "F5, F6", TRUE));
			gtk_entry_set_text (GTK_ENTRY (entry), "UNICODE 日本語");
			g_assert_true (row ("unicode action — 日本語 argument", "Ctrl+J", TRUE));
			gtk_widget_grab_focus (tree);
			g_assert_true (key (dialog, GDK_Return, 0));
			g_assert_cmpuint (dispatched, ==, 1);
			open_search ();
			gtk_entry_set_text (GTK_ENTRY (entry), "unicode");
			g_assert_true (row ("unicode action", "", FALSE));
			g_assert_true (row ("unicode action — 日本語 argument", "Ctrl+J", TRUE));
			e2_action_unregister ("test.unicode_action");
			g_signal_emit_by_name (entry, "activate");
			g_assert_nonnull (popup ());
			g_assert_cmpuint (dispatched, ==, 1);
			key (dialog, GDK_Escape, 0);
			open_search ();
			gtk_entry_set_text (GTK_ENTRY (entry), "unicode");
			g_assert_cmpint (gtk_tree_model_iter_n_children (gtk_tree_view_get_model (GTK_TREE_VIEW (tree)), NULL), ==, 0);
			key (dialog, GDK_Escape, 0);
			e2_pane_activate_other ();
			open_search ();
			key (dialog, GDK_Escape, 0);
			g_assert_true (curr_pane == &app.pane2);
#ifdef E2_VTE
			e2_option_str_set_direct (e2_option_get ("terminal-shell"), "/bin/sh");
			OPENBGL
			e2_action_run_simple_from ("terminal.open_here", NULL, app.main_window);
			CLOSEBGL
			break;
		case 4:
			g_assert_true (e2_terminal_has_focus ());
			key (app.main_window, GDK_A, GDK_CONTROL_MASK | GDK_SHIFT_MASK);
			g_assert_null (popup ());
			g_assert_true (e2_terminal_has_focus ());
			OPENBGL
			g_assert_false (e2_action_run_simple_from ("actions.search", NULL, app.main_window));
			CLOSEBGL
#endif
			finish ();
			return FALSE;
	}
	step++;
	OPENBGL
	return TRUE;
}

__attribute__((constructor)) static void schedule (void)
{
	unsetenv ("LD_PRELOAD");
	g_timeout_add (150, tick, NULL);
}
