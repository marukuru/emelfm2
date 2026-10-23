/* Type-ahead search in a file list: arrow keys must move straight to the
 * next or previous match, without visiting other rows on the way. */
#include "emelfm2.h"
#include "e2_action.h"
#include <gdk/gdkkeysyms.h>

static GArray *visited;	/* rows the cursor moved to during one key press */
static guint scrolls;	/* vertical scroll changes during one key press */

static void cursor_changed (GtkTreeView *tree, gpointer data)
{
	GtkTreePath *path = NULL;
	gtk_tree_view_get_cursor (tree, &path, NULL);
	if (path != NULL)
	{
		gint row = *gtk_tree_path_get_indices (path);
		g_array_append_val (visited, row);
		gtk_tree_path_free (path);
	}
}

static void scrolled (GtkAdjustment *adjustment, gpointer data)
{
	scrolls++;
}

static void key (GtkWidget *window, guint keyval)
{
	GdkEvent *event = gdk_event_new (GDK_KEY_PRESS);
	event->key.keyval = keyval;
	event->key.window = g_object_ref (gtk_widget_get_window (window));
	gunichar c = gdk_keyval_to_unicode (keyval);
	if (c != 0)
	{
		gchar text[7] = {0};
		g_unichar_to_utf8 (c, text);
		event->key.string = g_strdup (text);
		event->key.length = strlen (text);
	}
	GdkKeymapKey *keys;
	gint count;
	if (gdk_keymap_get_entries_for_keyval (gdk_keymap_get_default (), keyval, &keys, &count))
	{
		event->key.hardware_keycode = keys[0].keycode;
		event->key.group = keys[0].group;
		g_free (keys);
	}
#ifdef USE_GTK3_0
	GdkDevice *pointer = gdk_device_manager_get_client_pointer (
		gdk_display_get_device_manager (gdk_display_get_default ()));
	gdk_event_set_device (event, gdk_device_get_associated_device (pointer));
#endif
	gtk_main_do_event (event);
	gdk_event_free (event);
	while (gtk_events_pending ())
		gtk_main_iteration ();
}

/* GTK's interactive search runs in a separate popup window. */
static GtkWidget *search_window (void)
{
	GList *windows = gtk_window_list_toplevels (), *member;
	GtkWidget *found = NULL;
	for (member = windows; member != NULL; member = member->next)
		if (member->data != app.main_window
			&& gtk_window_get_window_type (member->data) == GTK_WINDOW_POPUP
			&& gtk_widget_get_visible (member->data))
			found = member->data;
	g_list_free (windows);
	return found;
}

static void expect_press (GtkWidget *window, guint keyval, gint row)
{
	g_array_set_size (visited, 0);
	scrolls = 0;
	key (window, keyval);
	GString *rows = g_string_new (NULL);
	guint i;
	for (i = 0; i < visited->len; i++)
		g_string_append_printf (rows, " %d", g_array_index (visited, gint, i));
	g_print ("key %s: expected row %d, cursor visited%s, %u scroll change(s)\n",
		gdk_keyval_name (keyval), row, rows->str, scrolls);
	g_string_free (rows, TRUE);
	g_assert_cmpuint (visited->len, ==, 1);
	g_assert_cmpint (g_array_index (visited, gint, 0), ==, row);
	g_assert_cmpuint (scrolls, <=, 1);
	g_assert_cmpint (curr_view->row, ==, row);
}

static gboolean tick (gpointer data)
{
	if (app.main_window == NULL || curr_view == NULL || !app.pane1.view.dir[0]
		|| g_atomic_int_get (&app.pane1.view.listcontrols.cd_working)
		|| g_atomic_int_get (&app.pane2.view.listcontrols.cd_working)) return TRUE;
	CLOSEBGL
#ifdef USE_GTK3_0
	/* Count scroll jumps, not the frames of an animated scroll. */
	g_object_set (gtk_settings_get_default (), "gtk-enable-animations", FALSE, NULL);
#endif
	ViewInfo *view = curr_view;
	GtkTreeView *tree = GTK_TREE_VIEW (view->treeview);
	/* Rows of every name which the typed prefix matches, in display order. */
	GArray *matches = g_array_new (FALSE, FALSE, sizeof (gint));
	GtkTreeIter iter;
	gint row = 0;
	if (gtk_tree_model_get_iter_first (view->model, &iter))
	do
	{
		gchar *name;
		gtk_tree_model_get (view->model, &iter, FILENAME, &name, -1);
		if (g_str_has_prefix (name, "quick-"))
			g_array_append_val (matches, row);
		g_free (name);
		row++;
	} while (gtk_tree_model_iter_next (view->model, &iter));
	g_assert_cmpuint (matches->len, >=, 20);

	/* Xvfb has no window manager to deliver a focus-in event. */
	GdkEvent *focus = gdk_event_new (GDK_FOCUS_CHANGE);
	focus->focus_change.window = g_object_ref (gtk_widget_get_window (app.main_window));
	focus->focus_change.in = TRUE;
	gtk_widget_event (app.main_window, focus);
	gdk_event_free (focus);
	gtk_widget_grab_focus (view->treeview);
	e2_fileview_focus_row (view, 0, TRUE, TRUE, FALSE, TRUE);

	visited = g_array_new (FALSE, FALSE, sizeof (gint));
	g_signal_connect (tree, "cursor-changed", G_CALLBACK (cursor_changed), NULL);
	GtkAdjustment *adjustment =
#ifdef USE_GTK3_0
		gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (tree));
#else
		gtk_tree_view_get_vadjustment (tree);
#endif
	g_signal_connect (adjustment, "value-changed", G_CALLBACK (scrolled), NULL);

	/* The first typed character opens the search and finds the first match. */
	g_array_set_size (visited, 0);
	key (app.main_window, GDK_q);
	GtkWidget *popup = search_window ();
	g_assert_nonnull (popup);
	g_assert_cmpuint (visited->len, >=, 1);
	g_assert_cmpint (g_array_index (visited, gint, visited->len - 1), ==,
		g_array_index (matches, gint, 0));
	g_assert_cmpint (curr_view->row, ==, g_array_index (matches, gint, 0));
	expect_press (popup, GDK_u, g_array_index (matches, gint, 0));

	guint i;
	for (i = 1; i < matches->len; i++)
		expect_press (popup, GDK_Down, g_array_index (matches, gint, i));
	for (i = matches->len - 1; i-- > 0; )
		expect_press (popup, GDK_Up, g_array_index (matches, gint, i));

	g_array_free (visited, TRUE);
	g_array_free (matches, TRUE);
	key (popup, GDK_Escape);
	g_print ("quick search arrow navigation passed\n");
	OPENBGL
	e2_action_run_simple_from ("command.quit", NULL, app.main_window);
	return FALSE;
}

__attribute__((constructor)) static void schedule (void)
{
	unsetenv ("LD_PRELOAD");
	g_timeout_add (500, tick, NULL);
}
