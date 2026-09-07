/* Column sizing and header menus for the two file panes.
 * Licensed under the GNU General Public License, version 3 or later.
 */
#include "emelfm2.h"

enum { E2_COL_AUTOMATIC = 1, E2_COL_LOCKED = 2 };
static gint column_flags[2][MAX_COLUMNS];
static gint flexible_column[2];
extern gint col_width_store[2][MAX_COLUMNS];

typedef struct
{
	GtkTreeView *treeview;
	GtkTreeModel *model;
	GtkTreeViewColumn *columns[MAX_COLUMNS];
	GtkTreeViewColumn *spacer;
	gint pane;
	gint available_width;
	guint idle;
	gboolean updating;
	gboolean measure;
} E2_ColumnSizing;

static gboolean _e2_columns_idle (gpointer user_data);

void e2_fileview_columns_register_cache (void)
{
	gint defaults[2][MAX_COLUMNS];
	gint pane, col;
	for (pane = 0; pane < 2; pane++)
		for (col = 0; col < MAX_COLUMNS; col++)
			defaults[pane][col] = E2_COL_AUTOMATIC;
	e2_cache_array_register ("columns-sizing", 2 * MAX_COLUMNS,
		(gint *) column_flags, (gint *) defaults);
	e2_cache_int_register ("pane1-flexible-column", &flexible_column[0], FILENAME);
	e2_cache_int_register ("pane2-flexible-column", &flexible_column[1], FILENAME);
	for (pane = 0; pane < 2; pane++)
		if (flexible_column[pane] < -1 || flexible_column[pane] >= MAX_COLUMNS)
			flexible_column[pane] = FILENAME;
}

//Callers own the list. The blank filler is not a model/configuration column.
GList *e2_fileview_get_columns (GtkTreeView *treeview)
{
	GList *columns = gtk_tree_view_get_columns (treeview), *node = columns;
	while (node != NULL)
	{
		GList *next = node->next;
		if (g_object_get_data (G_OBJECT (node->data), "e2-column-spacer") != NULL)
			columns = g_list_delete_link (columns, node);
		node = next;
	}
	return columns;
}

static GtkWidget *_e2_column_button (GtkTreeViewColumn *column)
{
#ifdef USE_GTK2_14
	return gtk_widget_get_ancestor (gtk_tree_view_column_get_widget (column),
		GTK_TYPE_BUTTON);
#else
	return column->button;
#endif
}

static GList *_e2_column_cells (GtkTreeViewColumn *column)
{
#ifdef USE_GTK2_12
	return gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (column));
#else
	return gtk_tree_view_column_get_cell_renderers (column);
#endif
}

static gint _e2_column_header_width (GtkTreeViewColumn *column)
{
	GtkWidget *button = _e2_column_button (column);
	GtkRequisition request;
	if (button != NULL)
	{
#ifdef USE_GTK3_0
		gtk_widget_get_preferred_size (button, NULL, &request);
#else
		gtk_widget_size_request (button, &request);
#endif
		return MAX (1, request.width);
	}
	return 1;
}

//Measure every row in the displayed model, including rows below the viewport.
//Use the actual renderers so font, padding and UTF-8 text are accounted for.
static gint _e2_column_optimal_width (E2_ColumnSizing *data, gint index)
{
	GtkTreeViewColumn *column = data->columns[index];
	GtkWidget *widget = GTK_WIDGET (data->treeview);
	gint width = _e2_column_header_width (column), separator = 0, focus = 0;
	gtk_widget_style_get (widget, "horizontal-separator", &separator,
		"focus-line-width", &focus, NULL);
	GList *cells = _e2_column_cells (column);
	GtkTreeModel *model = gtk_tree_view_get_model (data->treeview);
	GtkTreeIter iter;
	gboolean valid = model != NULL && gtk_tree_model_get_iter_first (model, &iter);
	while (valid)
	{
		GList *node;
		gint row_width = separator + 2 * focus;
		gtk_tree_view_column_cell_set_cell_data (column, model, &iter, FALSE, FALSE);
		for (node = cells; node != NULL; node = node->next)
		{
			gint cell_width;
			GtkCellRenderer *cell = node->data;
			PangoEllipsizeMode ellipsize;
			g_object_get (cell, "ellipsize", &ellipsize, NULL);
			g_object_set (cell, "ellipsize", PANGO_ELLIPSIZE_NONE, NULL);
#ifdef USE_GTK3_0
			gtk_cell_renderer_get_preferred_width (cell, widget, NULL, &cell_width);
#else
			gtk_cell_renderer_get_size (cell, widget, NULL, NULL, NULL,
				&cell_width, NULL);
#endif
			g_object_set (cell, "ellipsize", ellipsize, NULL);
			row_width += cell_width;
			if (node->next != NULL)
				row_width += gtk_tree_view_column_get_spacing (column);
		}
		width = MAX (width, row_width);
		valid = gtk_tree_model_iter_next (model, &iter);
	}
	g_list_free (cells);
	return width;
}

static void _e2_columns_apply (E2_ColumnSizing *data)
{
	gint i, pane = data->pane;
	gboolean expanding = FALSE;
	gint remaining = data->available_width;
	for (i = 0; i < MAX_COLUMNS; i++)
		if (gtk_tree_view_column_get_visible (data->columns[i])
			&& (flexible_column[pane] != i || (column_flags[pane][i] & E2_COL_LOCKED)))
			remaining -= MAX (1, col_width_store[pane][i]);
	data->updating = TRUE;
	for (i = 0; i < MAX_COLUMNS; i++)
	{
		GtkTreeViewColumn *column = data->columns[i];
		gboolean locked = (column_flags[pane][i] & E2_COL_LOCKED) != 0;
		gboolean flexible = flexible_column[pane] == i && !locked;
		gint width = MAX (1, col_width_store[pane][i]);
		//Reset both bounds before applying a different lock width.
		gtk_tree_view_column_set_min_width (column, -1);
		gtk_tree_view_column_set_max_width (column, -1);
		gtk_tree_view_column_set_fixed_width (column, flexible ?
			MAX (remaining, _e2_column_header_width (column)) : width);
		if (locked)
		{
			gtk_tree_view_column_set_min_width (column, width);
			gtk_tree_view_column_set_max_width (column, width);
		}
		//Assign the remaining width explicitly. GTK caches native expansion
		//even after fixed widths change, especially for an empty GTK 2 model.
		gtk_tree_view_column_set_expand (column, FALSE);
		gtk_tree_view_column_set_resizable (column, !locked && !flexible);
		if (flexible && gtk_tree_view_column_get_visible (column))
			expanding = TRUE;
	}
	//GTK ignores max-width on its last column when nothing expands. A blank
	//filler absorbs that space, keeping even the last real column locked.
	gtk_tree_view_column_set_visible (data->spacer, !expanding);
	data->updating = FALSE;
}

static void _e2_columns_size_allocate (GtkWidget *widget, GtkAllocation *allocation,
	E2_ColumnSizing *data)
{
	//This is the tree's viewport, already excluding the scrolled window's
	//border and vertical scrollbar; never subtract a guessed scrollbar width.
	//The adjustment/visible-rect may still describe the previous allocation.
	if (data->available_width != allocation->width)
	{
		data->available_width = allocation->width;
		//Apply after GTK finishes allocating; resize requests made inside its
		//allocation handler can be discarded. This path does not rescan rows.
		if (data->idle == 0)
			data->idle = g_idle_add (_e2_columns_idle, data);
	}
}

static gboolean _e2_columns_idle (gpointer user_data)
{
	E2_ColumnSizing *data = user_data;
	gint i, pane = data->pane;
	CLOSEBGL
	data->idle = 0;
	for (i = 0; i < MAX_COLUMNS; i++)
		if (data->measure && (column_flags[pane][i] & (E2_COL_AUTOMATIC | E2_COL_LOCKED))
				== E2_COL_AUTOMATIC && flexible_column[pane] != i
				&& gtk_tree_view_column_get_visible (data->columns[i]))
			col_width_store[pane][i] = _e2_column_optimal_width (data, i);
	data->measure = FALSE;
	_e2_columns_apply (data);
	OPENBGL
	return FALSE;
}

//Swapped signal handler: unused signal arguments follow data.
static void _e2_columns_queue (E2_ColumnSizing *data)
{
	if (!data->updating)
	{
		data->measure = TRUE;
		if (data->idle == 0)
			data->idle = g_idle_add (_e2_columns_idle, data);
	}
}

static void _e2_columns_model_changed (GObject *object, GParamSpec *pspec,
	E2_ColumnSizing *data)
{
	if (data->model != NULL)
	{
		g_signal_handlers_disconnect_by_func (data->model, _e2_columns_queue, data);
		g_object_unref (data->model);
	}
	data->model = gtk_tree_view_get_model (data->treeview);
	if (data->model != NULL)
	{
		g_object_ref (data->model);
		g_signal_connect_swapped (data->model, "row-inserted",
			G_CALLBACK (_e2_columns_queue), data);
		g_signal_connect_swapped (data->model, "row-deleted",
			G_CALLBACK (_e2_columns_queue), data);
		g_signal_connect_swapped (data->model, "row-changed",
			G_CALLBACK (_e2_columns_queue), data);
		g_signal_connect_swapped (data->model, "rows-reordered",
			G_CALLBACK (_e2_columns_queue), data);
	}
	_e2_columns_queue (data);
}

static void _e2_column_width_changed (GtkTreeViewColumn *column,
	GParamSpec *pspec, E2_ColumnSizing *data)
{
	if (data->updating)
		return;
	gint index = atoi (gtk_tree_view_column_get_title (column));
	if (column_flags[data->pane][index] & E2_COL_LOCKED)
	{
		//GTK 2 may defer a notify until after the restoring setter returns.
		if (gtk_tree_view_column_get_fixed_width (column) != col_width_store[data->pane][index])
			_e2_columns_apply (data);
	}
	else
	{
		col_width_store[data->pane][index] =
			gtk_tree_view_column_get_fixed_width (column);
		column_flags[data->pane][index] &= ~E2_COL_AUTOMATIC;
		_e2_columns_queue (data);
	}
}

static gboolean _e2_column_can_drop (GtkTreeView *treeview,
	GtkTreeViewColumn *column, GtkTreeViewColumn *previous,
	GtkTreeViewColumn *next, gpointer user_data)
{
	E2_ColumnSizing *data = user_data;
	return column != data->spacer && previous != data->spacer;
}

#ifndef USE_GTK3_0
static void _e2_column_allocated_width_changed (GtkTreeViewColumn *column,
	GParamSpec *pspec, E2_ColumnSizing *data)
{
	//GTK 2 mouse/keyboard resizing reports width, without updating fixed-width.
	//Its public GTK 2 struct distinguishes those changes from temporary extra
	//space GTK may allocate while our deferred layout is still pending.
	gint index = atoi (gtk_tree_view_column_get_title (column));
	gint width = gtk_tree_view_column_get_width (column);
	if (!data->updating && column->use_resized_width
		&& flexible_column[data->pane] != index
		&& !(column_flags[data->pane][index] & E2_COL_LOCKED)
		&& width != gtk_tree_view_column_get_fixed_width (column))
		gtk_tree_view_column_set_fixed_width (column, width);
}
#endif

static void _e2_columns_destroy (GtkWidget *widget, E2_ColumnSizing *data)
{
	gint i;
	g_signal_handlers_disconnect_by_data (widget, data);
	gtk_tree_view_set_column_drag_function (data->treeview, NULL, NULL, NULL);
	for (i = 0; i < MAX_COLUMNS; i++)
	{
		GList *cells = _e2_column_cells (data->columns[i]), *node;
		for (node = cells; node != NULL; node = node->next)
			g_signal_handlers_disconnect_by_data (node->data, data);
		g_list_free (cells);
		g_signal_handlers_disconnect_by_data (data->columns[i], data);
		g_object_set_data (G_OBJECT (data->columns[i]), "e2-column-sizing", NULL);
	}
	if (data->idle != 0)
		g_source_remove (data->idle);
	if (data->model != NULL)
	{
		g_signal_handlers_disconnect_by_func (data->model, _e2_columns_queue, data);
		g_object_unref (data->model);
	}
}

void e2_fileview_columns_init (GtkTreeView *treeview, gint pane)
{
	E2_ColumnSizing *data = g_new0 (E2_ColumnSizing, 1);
	GList *columns = e2_fileview_get_columns (treeview), *node;
	data->treeview = treeview;
	data->pane = pane;
	for (node = columns; node != NULL; node = node->next)
	{
		GtkTreeViewColumn *column = node->data;
		gint index = atoi (gtk_tree_view_column_get_title (column));
		data->columns[index] = column;
		g_object_set_data (G_OBJECT (column), "e2-column-sizing", data);
		g_signal_connect (column, "notify::fixed-width",
			G_CALLBACK (_e2_column_width_changed), data);
#ifndef USE_GTK3_0
		g_signal_connect (column, "notify::width",
			G_CALLBACK (_e2_column_allocated_width_changed), data);
#endif
		g_signal_connect_swapped (column, "notify::visible",
			G_CALLBACK (_e2_columns_queue), data);
		GList *cells = _e2_column_cells (column), *cell;
		for (cell = cells; cell != NULL; cell = cell->next)
			g_signal_connect_swapped (cell->data, "notify::font-desc",
				G_CALLBACK (_e2_columns_queue), data);
		g_list_free (cells);
	}
	g_list_free (columns);
	data->spacer = gtk_tree_view_column_new ();
	g_object_set_data (G_OBJECT (data->spacer), "e2-column-spacer", GINT_TO_POINTER (1));
	g_object_set (data->spacer, "sizing", GTK_TREE_VIEW_COLUMN_FIXED,
		"fixed-width", 1, "expand", TRUE, NULL);
	gtk_tree_view_column_set_widget (data->spacer, gtk_label_new (""));
	gtk_tree_view_append_column (treeview, data->spacer);
#ifdef USE_GTK3_0
	//The filler can legitimately be only one pixel wide. Remove the theme's
	//button padding/minimum so its empty header can also fit in that space.
	GtkCssProvider *css = gtk_css_provider_new ();
	gtk_css_provider_load_from_data (css,
		"* { padding: 0; border-width: 0;"
#ifdef USE_GTK3_20
		"min-width: 0; min-height: 0;"
#endif
		" }", -1, NULL);
	gtk_style_context_add_provider (gtk_widget_get_style_context (_e2_column_button (data->spacer)),
		GTK_STYLE_PROVIDER (css), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	g_object_unref (css);
#endif
	gtk_tree_view_set_column_drag_function (treeview, _e2_column_can_drop, data, NULL);
	g_object_set_data_full (G_OBJECT (treeview), "e2-column-sizing", data, g_free);
	g_signal_connect (treeview, "notify::model",
		G_CALLBACK (_e2_columns_model_changed), data);
	g_signal_connect (treeview, "size-allocate",
		G_CALLBACK (_e2_columns_size_allocate), data);
#ifdef USE_GTK3_0
	g_signal_connect_swapped (treeview, "style-updated",
#else
	g_signal_connect_swapped (treeview, "style-set",
#endif
		G_CALLBACK (_e2_columns_queue), data);
	g_signal_connect (treeview, "destroy", G_CALLBACK (_e2_columns_destroy), data);
	_e2_columns_apply (data);
	_e2_columns_model_changed (G_OBJECT (treeview), NULL, data);
}

static void _e2_column_lock (GtkCheckMenuItem *item, GtkTreeViewColumn *column)
{
	E2_ColumnSizing *data = g_object_get_data (G_OBJECT (column), "e2-column-sizing");
	gint index = atoi (gtk_tree_view_column_get_title (column));
	if (gtk_check_menu_item_get_active (item))
	{
		col_width_store[data->pane][index] = MAX (1, gtk_tree_view_column_get_width (column));
		column_flags[data->pane][index] |= E2_COL_LOCKED;
	}
	else
		column_flags[data->pane][index] &= ~E2_COL_LOCKED;
	_e2_columns_apply (data);
	_e2_columns_queue (data);
}

static void _e2_column_optimal (GtkMenuItem *item, GtkTreeViewColumn *column)
{
	E2_ColumnSizing *data = g_object_get_data (G_OBJECT (column), "e2-column-sizing");
	gint index = atoi (gtk_tree_view_column_get_title (column));
	if (column_flags[data->pane][index] & E2_COL_LOCKED)
		return;
	if (flexible_column[data->pane] == index)
		flexible_column[data->pane] = -1;
	column_flags[data->pane][index] |= E2_COL_AUTOMATIC;
	col_width_store[data->pane][index] = _e2_column_optimal_width (data, index);
	_e2_columns_apply (data);
}

static void _e2_column_flexible (GtkCheckMenuItem *item, GtkTreeViewColumn *column)
{
	E2_ColumnSizing *data = g_object_get_data (G_OBJECT (column), "e2-column-sizing");
	gint index = atoi (gtk_tree_view_column_get_title (column));
	if (gtk_check_menu_item_get_active (item))
	{
		if (flexible_column[data->pane] != -1 ||
			(column_flags[data->pane][index] & E2_COL_LOCKED))
			return;
		flexible_column[data->pane] = index;
	}
	else
		flexible_column[data->pane] = -1;
	_e2_columns_apply (data);
	_e2_columns_queue (data);
}

static gboolean _e2_column_menu_destroy (gpointer menu)
{
	gtk_widget_destroy (GTK_WIDGET (menu));
	return FALSE;
}

static void _e2_column_menu_done (GtkWidget *menu, gpointer unused)
{
	//Allow GTK and the selected item's activation to finish first.
	g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, _e2_column_menu_destroy,
		g_object_ref (menu), g_object_unref);
}

gboolean e2_fileview_column_menu (GtkTreeView *treeview, GtkWidget *header,
	GdkEventButton *event)
{
	E2_ColumnSizing *data = g_object_get_data (G_OBJECT (treeview), "e2-column-sizing");
	gint index;
	for (index = 0; index < MAX_COLUMNS; index++)
		if (_e2_column_button (data->columns[index]) == header)
			break;
	if (index == MAX_COLUMNS)
		return FALSE;
	GtkTreeViewColumn *column = data->columns[index];
	gboolean locked = (column_flags[data->pane][index] & E2_COL_LOCKED) != 0;
	gint flexible = flexible_column[data->pane];
	GtkWidget *menu = gtk_menu_new ();
	gtk_menu_attach_to_widget (GTK_MENU (menu), GTK_WIDGET (treeview), NULL);
	g_signal_connect_object (treeview, "destroy", G_CALLBACK (gtk_widget_destroy),
		menu, G_CONNECT_SWAPPED);
	g_signal_connect (menu, "selection-done", G_CALLBACK (_e2_column_menu_done), NULL);
	GtkWidget *item = gtk_check_menu_item_new_with_label (_("Lock"));
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), locked);
	g_signal_connect (item, "toggled", G_CALLBACK (_e2_column_lock), column);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	item = gtk_menu_item_new_with_label (_("Optimal column width"));
	gtk_widget_set_sensitive (item, !locked);
	g_signal_connect (item, "activate", G_CALLBACK (_e2_column_optimal), column);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	item = gtk_check_menu_item_new_with_label (_("Flexible"));
	gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item), flexible == index);
	gtk_widget_set_sensitive (item, !locked && (flexible == -1 || flexible == index));
	g_signal_connect (item, "toggled", G_CALLBACK (_e2_column_flexible), column);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	gtk_widget_show_all (menu);
#ifdef USE_GTK3_22
	gtk_menu_popup_at_pointer (GTK_MENU (menu), (GdkEvent *) event);
#else
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, event->button, event->time);
#endif
	return TRUE;
}
