/* GTK integration tests: run with tests/fileview_columns.sh. */
#include "../src/e2_fileview_columns.c"
#include <string.h>

gint col_width_store[2][MAX_COLUMNS];
pthread_mutex_t display_mutex = PTHREAD_MUTEX_INITIALIZER;
void e2_main_close_uilock (void) {}
void e2_main_open_uilock (void) {}

void e2_cache_array_register (gchar *name, guint size, gint *values, gint *defs)
{
	memcpy (values, defs, size * sizeof (gint));
}
void e2_cache_int_register (gchar *name, gint *value, gint def)
{
	*value = def;
}

static void settle (void)
{
	gint i;
	for (i = 0; i < 40; i++)
	{
		while (gtk_events_pending ())
			gtk_main_iteration ();
		g_usleep (5000);
	}
}

static GtkWidget *make_view (gint pane, GtkListStore **store,
	E2_ColumnSizing **data)
{
	GType types[MAX_COLUMNS];
	gint i;
	for (i = 0; i < MAX_COLUMNS; i++)
		types[i] = G_TYPE_STRING;
	*store = gtk_list_store_newv (MAX_COLUMNS, types);
	GtkWidget *tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL (*store));
	for (i = 0; i < MAX_COLUMNS; i++)
	{
		gchar title[16];
		g_snprintf (title, sizeof (title), "%d", i);
		GtkCellRenderer *cell = gtk_cell_renderer_text_new ();
		GtkTreeViewColumn *col = gtk_tree_view_column_new_with_attributes
			(title, cell, "text", i, NULL);
		gtk_tree_view_column_set_widget (col, gtk_label_new (i == 0 ? "Filename" : "Size"));
		gtk_widget_show (gtk_tree_view_column_get_widget (col));
		gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);
		gtk_tree_view_column_set_fixed_width (col, 80);
		gtk_tree_view_column_set_clickable (col, TRUE);
		gtk_tree_view_column_set_reorderable (col, TRUE);
		gtk_tree_view_append_column (GTK_TREE_VIEW (tree), col);
		if (col_width_store[pane][i] <= 0)
			col_width_store[pane][i] = 80;
	}
	e2_fileview_columns_init (GTK_TREE_VIEW (tree), pane);
	*data = g_object_get_data (G_OBJECT (tree), "e2-column-sizing");
	GtkWidget *sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add (GTK_CONTAINER (sw), tree);
	return sw;
}

static GtkWidget *menu_for (E2_ColumnSizing *data, gint index)
{
	GdkEvent *event = gdk_event_new (GDK_BUTTON_PRESS);
	event->button.button = 3;
	event->button.time = GDK_CURRENT_TIME;
	event->button.window = g_object_ref (gtk_widget_get_window (GTK_WIDGET (data->treeview)));
#ifdef USE_GTK3_0
	gdk_event_set_device (event, gdk_seat_get_pointer (
		gdk_display_get_default_seat (gdk_display_get_default ())));
#endif
	g_assert (e2_fileview_column_menu (data->treeview,
		_e2_column_button (data->columns[index]), &event->button));
	gdk_event_free (event);
	return gtk_menu_get_for_attach_widget (GTK_WIDGET (data->treeview))->data;
}

static GtkWidget *menu_item (GtkWidget *menu, gint index)
{
	GList *items = gtk_container_get_children (GTK_CONTAINER (menu));
	GtkWidget *item = g_list_nth_data (items, index);
	g_list_free (items);
	return item;
}

static void activate (E2_ColumnSizing *data, gint index, gint item)
{
	GtkWidget *menu = menu_for (data, index);
	gtk_menu_shell_activate_item (GTK_MENU_SHELL (menu), menu_item (menu, item), TRUE);
	settle ();
}

static void assert_fits (GtkWidget *sw, E2_ColumnSizing *data)
{
	GtkAdjustment *adjustment = gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (sw));
	g_assert_cmpfloat (gtk_adjustment_get_upper (adjustment), <=,
		gtk_adjustment_get_page_size (adjustment));
	GdkRectangle visible;
	gtk_tree_view_get_visible_rect (data->treeview, &visible);
	gint total = 0, i;
	for (i = 0; i < MAX_COLUMNS; i++)
		if (gtk_tree_view_column_get_visible (data->columns[i]))
			total += gtk_tree_view_column_get_width (data->columns[i]);
	g_assert_cmpint (total, ==, visible.width);
}

int main (int argc, char **argv)
{
	gtk_init (&argc, &argv);
	e2_fileview_columns_register_cache ();
	GtkListStore *store, *other_store;
	E2_ColumnSizing *data, *other;
	GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	GtkWidget *other_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	GtkWidget *sw = make_view (0, &store, &data);
	GtkWidget *other_sw = make_view (1, &other_store, &other);
	gtk_container_add (GTK_CONTAINER (window), sw);
	gtk_container_add (GTK_CONTAINER (other_window), other_sw);
	gtk_window_set_default_size (GTK_WINDOW (window), 1000, 250);
	gtk_window_set_default_size (GTK_WINDOW (other_window), 1000, 250);
	GtkTreeIter iter, longest;
	gint row, col;
	for (row = 0; row < 150; row++)
	{
		gtk_list_store_append (store, &iter);
		for (col = 0; col < MAX_COLUMNS; col++)
			gtk_list_store_set (store, &iter, col, col == 0 ?
				"a very long filename which should use only the remaining panel width.txt" : "12", -1);
	}
	longest = iter;
	gtk_list_store_set (store, &longest, SIZE, "12345678901234567890", -1);
	gtk_widget_show_all (window);
	gtk_widget_show_all (other_window);
	settle ();
	assert_fits (sw, data);
	gint wide = gtk_tree_view_column_get_width (data->columns[SIZE]);
	g_assert_cmpint (wide, >, 100); //widest row is below the viewport
	gint filename_width = gtk_tree_view_column_get_width (data->columns[FILENAME]);
	gint initial_width, initial_height;
	gtk_window_get_size (GTK_WINDOW (window), &initial_width, &initial_height);
	gtk_window_resize (GTK_WINDOW (window), initial_width - 100, 250);
	settle ();
	assert_fits (sw, data);
	g_assert_cmpint (gtk_tree_view_column_get_width (data->columns[FILENAME]), <, filename_width);
	gtk_list_store_set (store, &longest, SIZE, "12", -1);
	settle ();
	g_assert_cmpint (gtk_tree_view_column_get_width (data->columns[SIZE]), <, wide);
	assert_fits (sw, data);
	g_print ("PASS: automatic sizing, offscreen rows, shrink, resize and scrollbar\n");

#ifndef USE_GTK3_0
	//Exercise GTK 2's real keyboard resize path (also used by separator drags).
	gtk_window_present (GTK_WINDOW (window));
	gtk_widget_grab_focus (_e2_column_button (data->columns[SIZE]));
	settle ();
	g_assert (gtk_widget_has_focus (_e2_column_button (data->columns[SIZE])));
	GdkEvent *key = gdk_event_new (GDK_KEY_PRESS);
	key->key.window = g_object_ref (gtk_widget_get_window (GTK_WIDGET (data->treeview)));
	key->key.keyval = GDK_Right;
	key->key.state = GDK_SHIFT_MASK | GDK_MOD1_MASK;
	gint before = gtk_tree_view_column_get_width (data->columns[SIZE]);
	gtk_widget_event (GTK_WIDGET (data->treeview), key);
	gdk_event_free (key);
	settle ();
	g_assert_cmpint (gtk_tree_view_column_get_width (data->columns[SIZE]), ==, before + 2);
	g_assert_cmpint (col_width_store[0][SIZE], ==, before + 2);
	g_assert (!(column_flags[0][SIZE] & E2_COL_AUTOMATIC));
	activate (data, SIZE, 1);
	g_assert_cmpint (gtk_tree_view_column_get_width (data->columns[SIZE]), ==, before);
	g_print ("PASS: native GTK 2 resizing is saved and optimal resets it\n");
#endif

	GtkWidget *menu = menu_for (data, SIZE);
	g_assert (!gtk_widget_get_sensitive (menu_item (menu, 2)));
	gtk_widget_destroy (menu);
	activate (data, FILENAME, 2); //release flexible selection
	activate (data, SIZE, 2);
	g_assert_cmpint (flexible_column[0], ==, SIZE);
	g_assert_cmpint (flexible_column[1], ==, FILENAME);
	assert_fits (sw, data);
	activate (data, SIZE, 0); //lock flexible width
	gint locked = gtk_tree_view_column_get_width (data->columns[SIZE]);
	gtk_window_resize (GTK_WINDOW (window), 1200, 250);
	settle ();
	g_assert_cmpint (gtk_tree_view_column_get_width (data->columns[SIZE]), ==, locked);
	menu = menu_for (data, SIZE);
	g_assert (!gtk_widget_get_sensitive (menu_item (menu, 1)));
	gtk_widget_destroy (menu);
	activate (data, SIZE, 0); //unlock resumes flexibility
	assert_fits (sw, data);
	activate (data, SIZE, 1); //optimal turns off flexible
	g_assert_cmpint (flexible_column[0], ==, -1);
	activate (data, CHANGED, 0); //last real column must remain locked too
	locked = gtk_tree_view_column_get_width (data->columns[CHANGED]);
	gtk_window_resize (GTK_WINDOW (window), 1300, 250);
	settle ();
	g_assert_cmpint (gtk_tree_view_column_get_width (data->columns[CHANGED]), ==, locked);
	gtk_tree_view_column_set_fixed_width (data->columns[CHANGED], 500);
	settle ();
	g_assert_cmpint (gtk_tree_view_column_get_width (data->columns[CHANGED]), ==, locked);
	g_print ("PASS: menu exclusivity, pane independence, optimal, flexible/last-column locks\n");

	gtk_tree_view_column_set_fixed_width (data->columns[SIZE], 110);
	gtk_list_store_set (store, &longest, SIZE, "a longer value than before", -1);
	settle ();
	g_assert_cmpint (gtk_tree_view_column_get_width (data->columns[SIZE]), ==, 110);
	activate (data, SIZE, 1);
	g_assert_cmpint (gtk_tree_view_column_get_width (data->columns[SIZE]), >, 110);
	activate (data, FILENAME, 2);
	gtk_tree_view_move_column_after (data->treeview, data->columns[FILENAME], data->columns[CHANGED]);
	settle ();
	assert_fits (sw, data);
	GList *columns = e2_fileview_get_columns (data->treeview);
	g_assert_cmpint (g_list_length (columns), ==, MAX_COLUMNS);
	g_list_free (columns);
	gtk_tree_view_column_set_visible (data->columns[FILENAME], FALSE);
	settle ();
	g_assert (gtk_tree_view_column_get_visible (data->spacer));
	gtk_tree_view_column_set_visible (data->columns[FILENAME], TRUE);
	gtk_list_store_clear (store);
	settle ();
	assert_fits (sw, data);
	GtkAdjustment *vertical = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (sw));
	g_assert_cmpfloat (gtk_adjustment_get_upper (vertical), <=,
		gtk_adjustment_get_page_size (vertical));
	g_print ("PASS: manual sizing, reorder, hidden flexible column and empty directory\n");

	gtk_tree_view_set_model (data->treeview, GTK_TREE_MODEL (other_store));
	settle ();
	gtk_list_store_append (other_store, &iter);
	gtk_list_store_set (other_store, &iter, OWNER, "Unicode: 日本語 ÄÖÜ", -1);
	settle ();
	g_assert_cmpint (gtk_tree_view_column_get_width (data->columns[OWNER]), >, 70);
	gint font_width = gtk_tree_view_column_get_width (data->columns[OWNER]);
	GList *cells = _e2_column_cells (data->columns[OWNER]);
	g_object_set (cells->data, "font", "Sans 24", NULL);
	g_list_free (cells);
	settle ();
	g_assert_cmpint (gtk_tree_view_column_get_width (data->columns[OWNER]), >, font_width);
	gtk_list_store_set (other_store, &iter, OWNER, "pending update at destruction", -1);
	gtk_widget_destroy (window);
	gtk_widget_destroy (other_window);
	g_object_unref (store);
	g_object_unref (other_store);
	settle ();
	g_print ("PASS: model replacement, Unicode, font changes and destruction with pending work\n");
	window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	sw = make_view (0, &store, &data);
	gtk_container_add (GTK_CONTAINER (window), sw);
	gtk_window_set_default_size (GTK_WINDOW (window), 1000, 250);
	gtk_widget_show_all (window);
	settle ();
	g_assert_cmpint (flexible_column[0], ==, FILENAME);
	g_assert (!gtk_tree_view_column_get_resizable (data->columns[CHANGED]));
	g_assert_cmpint (gtk_tree_view_column_get_width (data->columns[CHANGED]), ==, locked);
	gtk_widget_destroy (window);
	g_object_unref (store);
	settle ();
	g_print ("PASS: settings survive view recreation\n");
	return 0;
}
