/* File-pane tabs. Licensed under GPL version 3 or later.
 * The legacy actions, plugins and directory workers refer to two fixed pane
 * addresses. Keep those runtimes stable and lazily restore a tab's state into
 * them. Only the selected notebook page owns the live pair of pane widgets.
 * Never copy runtime structs containing worker flags, hooks or widget pointers.
 */
#include "e2_tabs.h"
#include "e2_option.h"
#include "e2_pane.h"
#include "e2_filestore.h"
#include "e2_toolbar.h"
#include <gdk/gdkkeysyms.h>

typedef struct
{
	gchar *path;
	GList *history;
	guint history_position;
	GHashTable *selection;
	gdouble xscroll, yscroll;
	/* Only the filter/sort fields are used; no widgets or workers are saved. */
	ViewInfo settings;
} E2_TabPane;

typedef struct
{
	GtkWidget *page, *label;
	E2_TabPane panes[2];
	gboolean second_active;
	gdouble ratio;
	gboolean equal;
} E2_PaneTab;

static GtkWidget *notebook;
static GList *tabs;
static E2_PaneTab *current, *requested, *closing;
static guint switch_source, new_requests;
static gboolean selecting, loading, rebuilding, refresh_blocked;

static void _e2_tabs_schedule (void);

static void _e2_tabs_clear_pane (E2_TabPane *pane)
{
	g_free (pane->path);
	g_list_free (pane->history); /* Entries belong to app.dir_history. */
	if (pane->selection != NULL) g_hash_table_destroy (pane->selection);
	g_free (pane->settings.name_filter.patternptr);
	memset (pane, 0, sizeof (*pane));
}

static void _e2_tabs_save_pane (E2_TabPane *saved, E2_PaneRuntime *pane)
{
	ViewInfo *view = &pane->view;
	_e2_tabs_clear_pane (saved);
	saved->path = g_strdup (view->dir);
	HISTORY_LOCK
	saved->history = g_list_copy (pane->opendirs);
	saved->history_position = pane->opendir_cur;
	HISTORY_UNLOCK
	saved->selection = e2_fileview_log_selected_names (view);
	saved->xscroll = gtk_adjustment_get_value (gtk_scrolled_window_get_hadjustment
		(GTK_SCROLLED_WINDOW (pane->pane_sw)));
	saved->yscroll = gtk_adjustment_get_value (gtk_scrolled_window_get_vadjustment
		(GTK_SCROLLED_WINDOW (pane->pane_sw)));
	saved->settings.sort_column = view->sort_column;
	saved->settings.sort_order = view->sort_order;
	saved->settings.extsort = view->extsort;
	saved->settings.show_hidden = view->show_hidden;
	saved->settings.filter_directories = view->filter_directories;
	saved->settings.name_filter = view->name_filter;
	saved->settings.name_filter.patternptr = g_strdup (view->name_filter.patternptr);
	saved->settings.name_filter.compiled_patterns = NULL;
	saved->settings.size_filter = view->size_filter;
	saved->settings.date_filter = view->date_filter;
}

static void _e2_tabs_save (E2_PaneTab *tab)
{
	_e2_tabs_save_pane (&tab->panes[0], &app.pane1);
	_e2_tabs_save_pane (&tab->panes[1], &app.pane2);
	tab->second_active = (curr_pane == &app.pane2);
	tab->ratio = app.window.panes_paned_ratio;
	tab->equal = app.window.panes_equal;
}

static void _e2_tabs_label (E2_PaneTab *tab, const gchar *left, const gchar *right)
{
	gchar *a = g_path_get_basename ((left != NULL && *left) ? left : "/");
	gchar *b = g_path_get_basename ((right != NULL && *right) ? right : "/");
	gchar *title = g_strconcat (a, " | ", b, NULL);
	gtk_label_set_text (GTK_LABEL (tab->label), title);
#ifdef USE_GTK2_12
	gchar *tip = g_strdup_printf ("%s\n%s", left ? left : "/", right ? right : "/");
	gtk_widget_set_tooltip_text (tab->label, tip);
	g_free (tip);
#endif
	g_free (title);
	g_free (a);
	g_free (b);
}

void e2_tabs_update_title (void)
{
	if (current != NULL && !loading)
	{
		LISTS_LOCK
		gchar *left = g_strdup (app.pane1.view.dir);
		gchar *right = g_strdup (app.pane2.view.dir);
		LISTS_UNLOCK
		_e2_tabs_label (current, left, right);
		g_free (left);
		g_free (right);
	}
}

static gboolean _e2_tabs_busy (void)
{
	gboolean busy;
	LISTS_LOCK
	E2_Listman *a = &app.pane1.view.listcontrols;
	E2_Listman *b = &app.pane2.view.listcontrols;
	busy = a->newpath != NULL || b->newpath != NULL
		|| g_atomic_int_get (&a->cd_working) || g_atomic_int_get (&b->cd_working)
		|| g_atomic_int_get (&a->refresh_working) || g_atomic_int_get (&b->refresh_working)
		|| app.pane1.view.dir[0] == '\0' || app.pane2.view.dir[0] == '\0';
	LISTS_UNLOCK
	return busy;
}

static void _e2_tabs_restore_pane (E2_TabPane *saved, E2_PaneRuntime *pane)
{
	ViewInfo *view = &pane->view;
	HISTORY_LOCK
	g_list_free (pane->opendirs);
	pane->opendirs = g_list_copy (saved->history);
	pane->opendir_cur = saved->history_position;
	HISTORY_UNLOCK
	e2_fileview_clear_filter_patterns (view);
	g_free (view->name_filter.patternptr);
	view->name_filter = saved->settings.name_filter;
	view->name_filter.patternptr = g_strdup (saved->settings.name_filter.patternptr);
	view->size_filter = saved->settings.size_filter;
	view->date_filter = saved->settings.date_filter;
	view->filter_directories = saved->settings.filter_directories;
	view->show_hidden = saved->settings.show_hidden;
	/* Refilter even when two tabs display the same directory (no cd needed). */
	e2_fileview_refilter_list (view);
	e2_pane_restore_dir (pane, saved->path);
}

static void _e2_tabs_finish_pane (E2_TabPane *saved, E2_PaneRuntime *pane)
{
	ViewInfo *view = &pane->view;
	gint old_column = view->sort_column;
	view->sort_column = saved->settings.sort_column;
	view->sort_order = saved->settings.sort_order;
	view->extsort = saved->settings.extsort;
	GtkTreeSortable *sortable = GTK_TREE_SORTABLE (view->store);
	gtk_tree_sortable_set_sort_column_id (sortable,
		GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, GTK_SORT_ASCENDING);
	gtk_tree_sortable_set_sort_func (sortable, FILENAME,
		view->extsort ? (GtkTreeIterCompareFunc) e2_fileview_ext_sort
		: e2_all_columns[FILENAME].sort_func, &view->sort_order, NULL);
	gtk_tree_sortable_set_sort_column_id (sortable, view->sort_column, view->sort_order);
	GtkArrowType arrow = view->extsort
		? (view->sort_order == GTK_SORT_ASCENDING ? GTK_ARROW_RIGHT : GTK_ARROW_LEFT)
		: (view->sort_order == GTK_SORT_ASCENDING ? GTK_ARROW_DOWN : GTK_ARROW_UP);
#ifdef USE_GTK3_14
	(void) old_column;
	if (view->sort_arrow != NULL) gtk_widget_destroy (view->sort_arrow);
	e2_fileview_set_arrow (view, arrow);
#else
	if (old_column >= 0 && old_column < MAX_COLUMNS)
		gtk_widget_hide (view->sort_arrows[old_column]);
	gtk_arrow_set (GTK_ARROW (view->sort_arrows[view->sort_column]), arrow, GTK_SHADOW_NONE);
	gtk_widget_show (view->sort_arrows[view->sort_column]);
#endif
	gtk_tree_selection_unselect_all (view->selection);
	/* A removed directory may have failed to load. Don't apply its selection
	 * to the fallback directory; save the actual location on next departure. */
	if (strcmp (view->dir, saved->path) == 0)
	{
		e2_fileview_reselect_names (view, saved->selection, FALSE);
		gtk_adjustment_set_value (gtk_scrolled_window_get_hadjustment
			(GTK_SCROLLED_WINDOW (pane->pane_sw)), saved->xscroll);
		gtk_adjustment_set_value (gtk_scrolled_window_get_vadjustment
			(GTK_SCROLLED_WINDOW (pane->pane_sw)), saved->yscroll);
	}
	E2_ToggleType hidden = (pane == &app.pane1) ? E2_TOGGLE_PANE1HIDDEN : E2_TOGGLE_PANE2HIDDEN;
	E2_ToggleType filters = (pane == &app.pane1) ? E2_TOGGLE_PANE1FILTERS : E2_TOGGLE_PANE2FILTERS;
	e2_toolbar_toggle_button_set_state (toggles_array[hidden], view->show_hidden);
	e2_toolbar_toggle_button_set_state (toggles_array[filters],
		!(view->name_filter.active || view->size_filter.active || view->date_filter.active));
	e2_toolbar_toggle_filter_button (view);
}

static void _e2_tabs_remove (E2_PaneTab *tab)
{
	tabs = g_list_remove (tabs, tab);
	selecting = TRUE;
	gtk_widget_destroy (tab->page);
	selecting = FALSE;
	_e2_tabs_clear_pane (&tab->panes[0]);
	_e2_tabs_clear_pane (&tab->panes[1]);
	g_free (tab);
}

static void _e2_tabs_close (GtkWidget *button, E2_PaneTab *tab)
{
	NEEDCLOSEBGL
	if (tabs->next != NULL)
	{
		if (tab != current)
		{
			if (requested == tab) requested = NULL;
			if (closing == tab) closing = NULL;
			_e2_tabs_remove (tab);
		}
		else
		{
			GList *link = g_list_find (tabs, tab);
			requested = (link->next != NULL) ? link->next->data : link->prev->data;
			closing = tab;
			_e2_tabs_schedule ();
		}
	}
	NEEDOPENBGL
}

static E2_PaneTab *_e2_tabs_add (gboolean capture)
{
	E2_PaneTab *tab = g_new0 (E2_PaneTab, 1);
#ifdef USE_GTK3_0
	tab->page = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
	GtkWidget *labelbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 4);
#else
	tab->page = gtk_vbox_new (FALSE, 0);
	GtkWidget *labelbox = gtk_hbox_new (FALSE, 4);
#endif
	tab->label = gtk_label_new (NULL);
	gtk_label_set_ellipsize (GTK_LABEL (tab->label), PANGO_ELLIPSIZE_MIDDLE);
	gtk_label_set_max_width_chars (GTK_LABEL (tab->label), 32);
	GtkWidget *close = gtk_button_new_with_label ("×");
	gtk_button_set_relief (GTK_BUTTON (close), GTK_RELIEF_NONE);
#if GTK_CHECK_VERSION(3,20,0)
	gtk_widget_set_focus_on_click (close, FALSE);
#else
	gtk_button_set_focus_on_click (GTK_BUTTON (close), FALSE);
#endif
#ifdef USE_GTK2_12
	gtk_widget_set_tooltip_text (close, _("Close tab"));
#endif
	g_signal_connect (close, "clicked", G_CALLBACK (_e2_tabs_close), tab);
	gtk_box_pack_start (GTK_BOX (labelbox), tab->label, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (labelbox), close, FALSE, FALSE, 0);
	if (capture) _e2_tabs_save (tab);
	_e2_tabs_label (tab, tab->panes[0].path, tab->panes[1].path);
	tabs = g_list_append (tabs, tab);
	selecting = TRUE;
	gtk_notebook_append_page (GTK_NOTEBOOK (notebook), tab->page, labelbox);
	gtk_widget_show_all (labelbox);
	gtk_widget_show (tab->page);
	selecting = FALSE;
	return tab;
}

static gboolean _e2_tabs_process (gpointer data)
{
	CLOSEBGL
	if (!refresh_blocked)
	{
		e2_filestore_disable_refresh ();
		refresh_blocked = TRUE;
	}
	if (_e2_tabs_busy ())
	{
		OPENBGL
		return TRUE;
	}
	/* A refresh worker may have passed its suspension check just before we
	 * blocked it. Reserve both views with the same atomic claim it uses. */
	if (!g_atomic_int_compare_and_exchange (&app.pane1.view.listcontrols.refresh_working, 0, 1))
	{
		OPENBGL
		return TRUE;
	}
	if (!g_atomic_int_compare_and_exchange (&app.pane2.view.listcontrols.refresh_working, 0, 1))
	{
		g_atomic_int_set (&app.pane1.view.listcontrols.refresh_working, 0);
		OPENBGL
		return TRUE;
	}
	if (loading)
	{
		_e2_tabs_finish_pane (&current->panes[0], &app.pane1);
		_e2_tabs_finish_pane (&current->panes[1], &app.pane2);
		if (current->second_active != (curr_pane == &app.pane2))
			e2_pane_activate_other ();
		gtk_widget_set_sensitive (app.window.panes_outer_box, TRUE);
		gtk_widget_grab_focus (curr_view->treeview);
		loading = FALSE;
		e2_tabs_update_title ();
	}
	if (new_requests > 0)
	{
		new_requests--;
		requested = _e2_tabs_add (TRUE);
	}
	if (requested != NULL && requested != current)
	{
		_e2_tabs_save (current);
		GtkWidget *panes = app.window.panes_outer_box;
		g_object_ref (panes);
		gtk_container_remove (GTK_CONTAINER (current->page), panes);
		current = requested;
		requested = NULL;
		gtk_box_pack_start (GTK_BOX (current->page), panes, TRUE, TRUE, 0);
		g_object_unref (panes);
		selecting = TRUE;
		gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook),
			gtk_notebook_page_num (GTK_NOTEBOOK (notebook), current->page));
		selecting = FALSE;
		if (closing != NULL && closing != current)
		{
			_e2_tabs_remove (closing);
			closing = NULL;
		}
		loading = TRUE;
		gtk_widget_set_sensitive (panes, FALSE);
		app.window.panes_paned_ratio = current->ratio;
		app.window.panes_equal = current->equal;
		gint maxpos;
		g_object_get (app.window.panes_paned, "max-position", &maxpos, NULL);
		gtk_paned_set_position (GTK_PANED (app.window.panes_paned), maxpos * current->ratio);
		_e2_tabs_restore_pane (&current->panes[0], &app.pane1);
		_e2_tabs_restore_pane (&current->panes[1], &app.pane2);
		g_atomic_int_set (&app.pane1.view.listcontrols.refresh_working, 0);
		g_atomic_int_set (&app.pane2.view.listcontrols.refresh_working, 0);
		OPENBGL
		return TRUE;
	}
	requested = NULL;
	g_atomic_int_set (&app.pane1.view.listcontrols.refresh_working, 0);
	g_atomic_int_set (&app.pane2.view.listcontrols.refresh_working, 0);
	if (refresh_blocked)
	{
		e2_filestore_enable_refresh ();
		refresh_blocked = FALSE;
	}
	gboolean again = (new_requests > 0);
	if (!again) switch_source = 0;
	OPENBGL
	return again;
}

static void _e2_tabs_schedule (void)
{
	if (switch_source == 0 && !rebuilding)
		switch_source = g_timeout_add (30, _e2_tabs_process, NULL);
}

static void _e2_tabs_switch (GtkNotebook *book, gpointer page, guint number, gpointer data)
{
	if (selecting || current == NULL) return;
	/* Defer the actual selection until both directory workers are idle. */
	g_signal_stop_emission_by_name (book, "switch-page");
	NEEDCLOSEBGL
	requested = g_list_nth_data (tabs, number);
	_e2_tabs_schedule ();
	NEEDOPENBGL
}

void e2_tabs_pack (GtkWidget *panes)
{
	if (!e2_option_bool_get ("pane-tabs"))
	{
		e2_tabs_cleanup ();
		gtk_paned_pack1 (GTK_PANED (app.window.output_paned), panes, TRUE, TRUE);
		return;
	}
	if (notebook == NULL)
	{
		notebook = gtk_notebook_new ();
		gtk_widget_set_name (notebook, "file-pane-tabs");
		gtk_notebook_set_scrollable (GTK_NOTEBOOK (notebook), TRUE);
		gtk_notebook_set_show_border (GTK_NOTEBOOK (notebook), FALSE);
		g_signal_connect (notebook, "switch-page", G_CALLBACK (_e2_tabs_switch), NULL);
		current = _e2_tabs_add (FALSE);
		gtk_paned_pack1 (GTK_PANED (app.window.output_paned), notebook, TRUE, TRUE);
	}
	gtk_box_pack_start (GTK_BOX (current->page), panes, TRUE, TRUE, 0);
	gtk_widget_set_sensitive (panes, !loading);
	gtk_widget_show (notebook);
	e2_tabs_update_title ();
}

void e2_tabs_rebuild_begin (void)
{
	rebuilding = TRUE;
	if (switch_source != 0) g_source_remove (switch_source);
	switch_source = 0;
}

void e2_tabs_rebuild_end (void)
{
	rebuilding = FALSE;
	if (loading || requested != NULL || new_requests > 0 || refresh_blocked)
		_e2_tabs_schedule ();
}

void e2_tabs_cleanup (void)
{
	if (switch_source != 0) g_source_remove (switch_source);
	switch_source = 0;
	/* Shutdown still needs the live panes for cache/plugin cleanup. Disabling
	 * tabs during a rebuild has already destroyed the old pane widgets. */
	GtkWidget *panes = app.window.panes_outer_box;
	gboolean preserve = current != NULL && panes != NULL
		&& gtk_widget_get_parent (panes) == current->page;
	if (preserve)
	{
		g_object_ref (panes);
		gtk_container_remove (GTK_CONTAINER (current->page), panes);
	}
	current = requested = closing = NULL;
	while (tabs != NULL) _e2_tabs_remove (tabs->data);
	if (notebook != NULL) gtk_widget_destroy (notebook);
	notebook = NULL;
	if (preserve)
	{
		gtk_paned_pack1 (GTK_PANED (app.window.output_paned), panes, TRUE, TRUE);
		g_object_unref (panes);
	}
	new_requests = 0;
	loading = FALSE;
	if (refresh_blocked) e2_filestore_enable_refresh ();
	refresh_blocked = FALSE;
}

gboolean e2_tabs_key (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	if (notebook == NULL || gdk_keyval_to_lower (event->keyval) != GDK_n
		|| (event->state & gtk_accelerator_get_default_mod_mask ()) != GDK_CONTROL_MASK)
		return FALSE;
	NEEDCLOSEBGL
	new_requests++;
	_e2_tabs_schedule ();
	NEEDOPENBGL
	return TRUE;
}
