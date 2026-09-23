/* Action discovery dialog. Licensed under GPL version 3 or later. */
#include "emelfm2.h"
#include "e2_action_search.h"
#include "e2_action.h"
#include "e2_dialog.h"
#include "e2_command_line.h"
#include "e2_keybinding.h"
#include "e2_pane.h"
#include "e2_plugins.h"
#include "e2_terminal.h"
#include <gdk/gdkkeysyms.h>

extern GtkTreeStore *actions_store;

/* The registry's has_arg flag does not distinguish optional from required
 * arguments. These common actions have a useful, safe no-argument invocation.
 * All execution still goes through the registered action and its own dialogs. */
typedef struct
{
	gint group, action;
	const gchar *label, *aliases;
	gboolean selection;
} E2_SearchMetadata;

static const E2_SearchMetadata metadata[] =
{
	{6, 39, N_("Copy to other pane"), N_("copy duplicate files"), TRUE},
	{6, 40, N_("Copy as…"), N_("copy rename"), TRUE},
	{6, 42, N_("Copy with time"), N_("copy preserve timestamp"), TRUE},
	{6, 41, N_("Copy and merge"), N_("copy merge directories"), TRUE},
	{6, 65, N_("Move to other pane"), N_("move files"), TRUE},
	{6, 66, N_("Move as…"), N_("move rename"), TRUE},
	{6, 79, N_("Rename…"), N_("rename files"), TRUE},
	{1, 63, N_("New folder…"), N_("mkdir create directory new folder"), FALSE},
	{6, 18, N_("Move to trash"), N_("trash recycle bin"), TRUE},
	{6, 45, N_("Delete…"), N_("delete remove permanently"), TRUE},
	{6, 109, N_("View file"), N_("view preview read"), TRUE},
	{6, 46, N_("Edit file"), N_("edit editor"), TRUE},
	{6, 58, N_("File information"), N_("properties information details"), TRUE},
	{6, 73, N_("Permissions…"), N_("permissions chmod"), TRUE},
	{6, 70, N_("Ownership…"), N_("owner group chown"), TRUE},
	{6, 49, N_("Find in file list…"), N_("find search files"), FALSE},
	{13, 54, N_("Parent directory"), N_("up parent directory"), FALSE},
	{13, 52, N_("Previous directory"), N_("back history"), FALSE},
	{13, 53, N_("Next directory"), N_("forward history"), FALSE},
	{13, 67, N_("Choose directory…"), N_("open folder directory"), FALSE},
	{13, 87, N_("Show hidden files"), N_("toggle hidden dotfiles"), FALSE},
	{13, 25, N_("File filters…"), N_("filter name size date"), FALSE},
	{14, 98, N_("Switch active pane"), N_("switch panel"), FALSE},
	{14, 76, N_("Refresh panes"), N_("refresh reload"), FALSE},
	{7, 104, N_("Select all / none"), N_("toggle selection"), FALSE},
	{7, 60, N_("Invert selection"), N_("invert selection"), FALSE},
	{7, 83, N_("Select by extension"), N_("select file type"), FALSE},
	{18, 67, N_("Open trash"), N_("trash recycle bin"), FALSE},
	{3, 34, N_("Configure…"), N_("settings preferences options"), FALSE},
};

typedef struct
{
	gchar *name, *argument, *label, *category, *shortcut, *search;
	gint rank;
	gboolean needs_argument, needs_selection, plugin, allow_parent;
	gint entry_kind; /* 1: command entry, 2: directory entry */
} E2_SearchItem;

typedef struct
{
	GtkWidget *dialog, *entry, *tree, *summary, *reason, *empty, *origin;
	GtkListStore *store;
	GPtrArray *items;
	GList *bindings;
	E2_PaneRuntime *pane;
	GtkTreeSelection *pane_selection;
	guint selected;
	gchar *source, *destination;
	gulong selection_handler;
} E2_ActionSearch;

enum { LABEL, CATEGORY, SHORTCUT, AVAILABLE, ITEM, N_COLUMNS };
static GtkWidget *search_dialog;

static gchar *_e2_action_search_words (const gchar *text)
{
	gchar *folded = g_utf8_casefold (text, -1);
	gchar *normal = g_utf8_normalize (folded, -1, G_NORMALIZE_ALL_COMPOSE);
	g_free (folded);
	g_strdelimit (normal, "_.", ' ');
	return normal;
}

static gchar *_e2_action_search_label (const gchar *text)
{
	/* Remove menu mnemonics from plugin labels, without interpreting markup. */
	GString *label = g_string_new (NULL);
	const gchar *p;
	for (p = text; *p; p++)
	{
		if (*p == '_' && p[1] != '_') continue;
		if (*p == '_' && p[1] == '_') p++;
		g_string_append_c (label, *p);
	}
	return g_string_free (label, FALSE);
}

static PluginAction *_e2_action_search_plugin (const gchar *name)
{
	if (app.plugacts != NULL)
	{
		guint i;
		for (i = 0; i < app.plugacts->len; i++)
		{
			PluginAction *pa = g_ptr_array_index (app.plugacts, i);
			if (pa->aname != NULL && !strcmp (pa->aname, name) && IS_LOADED (pa))
				return pa;
		}
	}
	return NULL;
}

static gboolean _e2_action_search_executable (E2_Action *action)
{
	return action != NULL && action->func != NULL
		&& (action->type == E2_ACTION_TYPE_ITEM || action->type == E2_ACTION_TYPE_HOVER)
		&& !(action->exclude & (E2_ACTION_EXCLUDE_GENERAL | E2_ACTION_EXCLUDE_MENU
			| E2_ACTION_EXCLUDE_ACCEL | E2_ACTION_EXCLUDE_LAYOUT));
}

static void _e2_action_search_add (E2_ActionSearch *rt, E2_Action *action,
	const gchar *argument)
{
	if (!_e2_action_search_executable (action) || !strcmp (action->name, "actions.search")) return;
	guint i;
	for (i = 0; i < rt->items->len; i++)
	{
		E2_SearchItem *existing = g_ptr_array_index (rt->items, i);
		if (!strcmp (existing->name, action->name) && !strcmp (existing->argument, argument)) return;
	}
	E2_SearchItem *item = g_new0 (E2_SearchItem, 1);
	item->name = g_strdup (action->name);
	item->argument = g_strdup (argument);
	item->rank = G_MAXINT;
	item->needs_argument = action->has_arg && !*argument;
	const gchar *dot = strchr (action->name, '.');
	item->category = dot ? g_strndup (action->name, dot - action->name) : g_strdup (_("Actions"));
	item->label = g_strdup (dot ? dot + 1 : action->name);
	g_strdelimit (item->label, "_", ' ');
	item->needs_selection = !strcmp (item->category, _A(6));
	if (dot != NULL)
	{
		const gchar *verb = dot + 1;
		if (!strcmp (item->category, _A(7))) item->needs_argument = FALSE;
		if (item->needs_selection)
		{
			item->allow_parent = !strcmp (verb, _A(67)) || !strcmp (verb, _A(68));
			if (!strcmp (verb, _A(47)) || !strcmp (verb, _A(110)))
				item->needs_selection = FALSE; /* Reopen the previous editor/viewer. */
		}
		if ((!strcmp (item->category, _A(1)) || !strcmp (item->category, _A(5)))
			&& (!strcmp (verb, _A(36)) || !strcmp (verb, _A(37))
				|| !strcmp (verb, _A(44)) || !strcmp (verb, _A(38))))
			item->entry_kind = !strcmp (item->category, _A(1)) ? 1 : 2;
	}
	const gchar *description = "";
	for (i = 0; i < G_N_ELEMENTS (metadata); i++)
	{
		gchar *name = g_strconcat (_A(metadata[i].group), ".", _A(metadata[i].action), NULL);
		gboolean match = !strcmp (name, action->name);
		g_free (name);
		if (match)
		{
			g_free (item->label);
			item->label = g_strdup (_(metadata[i].label));
			description = _(metadata[i].aliases);
			item->rank = i;
			item->needs_argument = FALSE;
			item->needs_selection = metadata[i].selection;
			break;
		}
	}
	/* option.set historically advertises has_arg=FALSE, but needs a value. */
	gchar *option_set = g_strconcat (_A(9), ".", _A(85), NULL);
	if (!strcmp (action->name, option_set)) item->needs_argument = !*argument;
	g_free (option_set);
	PluginAction *pa = _e2_action_search_plugin (action->name);
	if (pa != NULL)
	{
		item->plugin = !*argument;
		item->needs_argument = FALSE;
		if (pa->label != NULL && *pa->label)
		{
			g_free (item->label);
			item->label = _e2_action_search_label (pa->label);
		}
		if (pa->description != NULL) description = pa->description;
	}
	const gchar *category = NULL;
	if (pa != NULL) category = _("Plugins");
	else if (!strcmp (item->category, _A(6))) category = _("Files");
	else if (!strcmp (item->category, _A(7))) category = _("Selection");
	else if (!strcmp (item->category, _A(13)) || !strcmp (item->category, _A(14)))
		category = _("Navigation");
	else if (!strcmp (item->category, _A(10))) category = _("Output");
	if (category != NULL)
	{
		g_free (item->category);
		item->category = g_strdup (category);
	}
	if (*argument)
	{
		if (pa == NULL && dot != NULL && g_str_has_prefix (action->name, _A(6)))
			item->needs_selection = FALSE; /* File task arguments supply the filenames. */
		gchar *label = g_strdup_printf ("%s — %s", item->label, argument);
		g_free (item->label);
		item->label = label;
	}
	GString *shortcuts = g_string_new (NULL);
	GList *member;
	for (member = rt->bindings; member != NULL; member = member->next)
	{
		E2_ActionBinding *binding = member->data;
		if (!strcmp (binding->action, item->name) && !strcmp (binding->argument, argument)
			&& !(item->plugin && pa->action_data != NULL))
		{
			if (shortcuts->len) g_string_append (shortcuts, ", ");
			g_string_append (shortcuts, binding->label);
		}
	}
	item->shortcut = g_string_free (shortcuts, FALSE);
	gchar *words = g_strconcat (item->name, " ", item->label, " ",
		item->category, " ", description, " ", item->shortcut, NULL);
	item->search = _e2_action_search_words (words);
	g_free (words);
	g_ptr_array_add (rt->items, item);
}

static gboolean _e2_action_search_collect (GtkTreeModel *model, GtkTreePath *path,
	GtkTreeIter *iter, gpointer data)
{
	gchar *name;
	gtk_tree_model_get (model, iter, 0, &name, -1);
	_e2_action_search_add (data, e2_action_get (name), "");
	g_free (name);
	return FALSE;
}

static gint _e2_action_search_compare (gconstpointer a, gconstpointer b)
{
	const E2_SearchItem *left = *(E2_SearchItem **)a, *right = *(E2_SearchItem **)b;
	if (left->rank != right->rank) return left->rank < right->rank ? -1 : 1;
	return g_utf8_collate (left->label, right->label);
}

static guint _e2_action_search_selection (E2_ActionSearch *rt)
{
	GList *selection = e2_fileview_get_selected_local (&rt->pane->view, FALSE);
	guint count = g_list_length (selection);
	g_list_free (selection);
	return count;
}

static const gchar *_e2_action_search_unavailable (E2_ActionSearch *rt, E2_SearchItem *item)
{
	if (curr_pane != rt->pane || strcmp (curr_view->dir, rt->source)
		|| strcmp (other_view->dir, rt->destination))
		return _("Pane context changed; reopen action search");
	if (!_e2_action_search_executable (e2_action_get (item->name)))
		return _("Action is no longer available");
	if (item->needs_argument) return _("Requires configured arguments");
	if (item->entry_kind != 0)
	{
		E2_CommandLineRuntime *line = (rt->origin != NULL && GTK_IS_ENTRY (rt->origin))
			? g_object_get_data (G_OBJECT (rt->origin), "command-line-runtime") : NULL;
		if (line == NULL || line->original != (item->entry_kind == 1))
			return item->entry_kind == 1 ? _("Focus the command entry first") : _("Focus a directory entry first");
	}
	if (item->needs_selection && rt->selected == 0
		&& !(item->allow_parent && gtk_tree_selection_count_selected_rows (rt->pane_selection) > 0))
		return _("Select at least one item");
	return NULL;
}

static E2_SearchItem *_e2_action_search_selected (E2_ActionSearch *rt)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	E2_SearchItem *item = NULL;
	if (gtk_tree_selection_get_selected (gtk_tree_view_get_selection
		(GTK_TREE_VIEW (rt->tree)), &model, &iter))
		gtk_tree_model_get (model, &iter, ITEM, &item, -1);
	return item;
}

static void _e2_action_search_selection_changed (GtkTreeSelection *selection, E2_ActionSearch *rt)
{
	E2_SearchItem *item = _e2_action_search_selected (rt);
	const gchar *reason = item ? _e2_action_search_unavailable (rt, item) : NULL;
	gtk_label_set_text (GTK_LABEL (rt->reason), reason ? reason : "");
}

static void _e2_action_search_filter (GtkEditable *editable, E2_ActionSearch *rt)
{
	rt->selected = _e2_action_search_selection (rt);
	gchar *query = _e2_action_search_words (gtk_entry_get_text (GTK_ENTRY (rt->entry)));
	gchar **tokens = g_strsplit_set (query, " \t\r\n", -1);
	gtk_list_store_clear (rt->store);
	guint i, count = 0;
	for (i = 0; i < rt->items->len; i++)
	{
		E2_SearchItem *item = g_ptr_array_index (rt->items, i);
		guint j;
		for (j = 0; tokens[j] != NULL; j++)
			if (*tokens[j] && strstr (item->search, tokens[j]) == NULL) break;
		if (tokens[j] != NULL) continue;
		GtkTreeIter iter;
		gtk_list_store_append (rt->store, &iter);
		gtk_list_store_set (rt->store, &iter, LABEL, item->label,
			CATEGORY, item->category, SHORTCUT, item->shortcut,
			AVAILABLE, _e2_action_search_unavailable (rt, item) == NULL, ITEM, item, -1);
		count++;
	}
	g_strfreev (tokens);
	g_free (query);
	if (count)
	{
		GtkTreePath *path = gtk_tree_path_new_first ();
		gtk_tree_view_set_cursor (GTK_TREE_VIEW (rt->tree), path, NULL, FALSE);
		gtk_tree_path_free (path);
		gtk_widget_hide (rt->empty);
	}
	else gtk_widget_show (rt->empty);
	guint selected = rt->selected;
	gchar *count_text = g_strdup_printf (ngettext ("%u selected item", "%u selected items", selected), selected);
	gchar *summary = g_strdup_printf (_("%s\nFrom: %s\nTo: %s"), count_text, rt->source, rt->destination);
	gtk_label_set_text (GTK_LABEL (rt->summary), summary);
#ifdef USE_GTK2_12
	gtk_widget_set_tooltip_text (rt->summary, summary);
#endif
	g_free (summary);
	g_free (count_text);
}

static void _e2_action_search_pane_selection (GtkTreeSelection *selection, E2_ActionSearch *rt)
{
	_e2_action_search_filter (NULL, rt);
}

static void _e2_action_search_destroy (GtkWidget *dialog, E2_ActionSearch *rt)
{
	search_dialog = NULL;
	g_signal_handler_disconnect (rt->pane_selection, rt->selection_handler);
	g_object_unref (rt->pane_selection);
	g_signal_handlers_disconnect_by_data (rt->entry, rt);
	g_signal_handlers_disconnect_by_data (rt->tree, rt);
	g_signal_handlers_disconnect_by_data (gtk_tree_view_get_selection (GTK_TREE_VIEW (rt->tree)), rt);
	if (rt->origin != NULL)
	{
		gtk_widget_grab_focus (rt->origin);
		g_object_remove_weak_pointer (G_OBJECT (rt->origin), (gpointer *)&rt->origin);
	}
	guint i;
	for (i = 0; i < rt->items->len; i++)
	{
		E2_SearchItem *item = g_ptr_array_index (rt->items, i);
		g_free (item->name); g_free (item->argument); g_free (item->label);
		g_free (item->category); g_free (item->shortcut); g_free (item->search);
		g_free (item);
	}
	g_ptr_array_free (rt->items, TRUE);
	e2_keybinding_action_bindings_free (rt->bindings);
	g_object_unref (rt->store);
	g_free (rt->source); g_free (rt->destination);
	g_free (rt);
}

static void _e2_action_search_run (E2_ActionSearch *rt)
{
	E2_SearchItem *item = _e2_action_search_selected (rt);
	if (item == NULL) return;
	rt->selected = _e2_action_search_selection (rt);
	const gchar *reason = _e2_action_search_unavailable (rt, item);
	if (reason != NULL)
	{
		gtk_label_set_text (GTK_LABEL (rt->reason), reason);
		return;
	}
	gchar *name = g_strdup (item->name), *argument = g_strdup (item->argument);
	gboolean plugin = item->plugin;
	GtkWidget *origin = rt->origin ? rt->origin : rt->pane->view.treeview;
	g_object_ref (origin);
	gtk_widget_destroy (rt->dialog);
	/* Look up the action again; a plugin may have been unloaded. Never pass an
 * unknown name through the command fallback or execute the search text. */
	E2_Action *action = e2_action_get (name);
	if (_e2_action_search_executable (action))
	{
		PluginAction *pa = plugin ? _e2_action_search_plugin (name) : NULL;
		E2_ActionRuntime art = {action, pa ? pa->action_data : argument, NULL, 0};
		OPENBGL
		e2_action_run (origin, &art);
		CLOSEBGL
	}
	g_object_unref (origin);
	g_free (argument); g_free (name);
}

static void _e2_action_search_activate (GtkEntry *entry, E2_ActionSearch *rt)
{
	_e2_action_search_run (rt);
}

static void _e2_action_search_row (GtkTreeView *tree, GtkTreePath *path,
	GtkTreeViewColumn *column, E2_ActionSearch *rt)
{
	_e2_action_search_run (rt);
}

static gboolean _e2_action_search_key (GtkWidget *widget, GdkEventKey *event, E2_ActionSearch *rt)
{
	if (event->keyval == GDK_Escape)
	{
		gtk_widget_destroy (rt->dialog);
		return TRUE;
	}
	if (gtk_window_get_focus (GTK_WINDOW (rt->dialog)) == rt->tree
		&& (event->keyval == GDK_Return || event->keyval == GDK_KP_Enter))
	{
		_e2_action_search_run (rt);
		return TRUE;
	}
	if (gtk_window_get_focus (GTK_WINDOW (rt->dialog)) == rt->entry
		&& (event->keyval == GDK_Up || event->keyval == GDK_Down))
	{
		GtkTreePath *path = NULL;
		gtk_tree_view_get_cursor (GTK_TREE_VIEW (rt->tree), &path, NULL);
		if (path == NULL) return TRUE;
		gint index = *gtk_tree_path_get_indices (path);
		gint count = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (rt->store), NULL);
		index = CLAMP (index + (event->keyval == GDK_Down ? 1 : -1), 0, count - 1);
		gtk_tree_path_free (path);
		path = gtk_tree_path_new_from_indices (index, -1);
		gtk_tree_view_set_cursor (GTK_TREE_VIEW (rt->tree), path, NULL, FALSE);
		gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (rt->tree), path, NULL, FALSE, 0, 0);
		gtk_tree_path_free (path);
		return TRUE;
	}
	return FALSE;
}

static void _e2_action_search_response (GtkDialog *dialog, gint response, gpointer data)
{
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

#ifdef USE_GTK2_16
static void _e2_action_search_clear (GtkEntry *entry, GtkEntryIconPosition position,
	GdkEvent *event, gpointer data)
{
	gtk_entry_set_text (entry, "");
}
#endif

static GtkWidget *_e2_action_search_text (GtkWidget *box, const gchar *text)
{
	GtkWidget *label = gtk_label_new (text);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_label_set_ellipsize (GTK_LABEL (label), PANGO_ELLIPSIZE_MIDDLE);
	gtk_box_pack_start (GTK_BOX (box), label, FALSE, FALSE, 0);
	return label;
}

static gboolean _e2_action_search_show (gpointer from, E2_ActionRuntime *art)
{
#ifdef E2_VTE
	if (e2_terminal_has_focus ()) return FALSE;
#endif
	if (search_dialog != NULL)
	{
		gtk_window_present (GTK_WINDOW (search_dialog));
		return TRUE;
	}
	if (curr_view == NULL || curr_view->dir[0] == '\0') return FALSE;
	E2_ActionSearch *rt = g_new0 (E2_ActionSearch, 1);
	rt->pane = curr_pane;
	rt->source = g_strdup (curr_view->dir);
	rt->destination = g_strdup (other_view->dir);
	rt->origin = gtk_window_get_focus (GTK_WINDOW (app.main_window));
	if (rt->origin != NULL)
		g_object_add_weak_pointer (G_OBJECT (rt->origin), (gpointer *)&rt->origin);
	rt->bindings = e2_keybinding_action_bindings (rt->origin);
	rt->items = g_ptr_array_new ();
	/* Rebuild on each opening: configuration and plugin changes are reflected
 * immediately, with no persistent cache, filesystem scan or polling timer. */
	gtk_tree_model_foreach (GTK_TREE_MODEL (actions_store), _e2_action_search_collect, rt);
	GList *member;
	for (member = rt->bindings; member != NULL; member = member->next)
	{
		E2_ActionBinding *binding = member->data;
		if (*binding->argument)
			_e2_action_search_add (rt, e2_action_get (binding->action), binding->argument);
	}
	g_ptr_array_sort (rt->items, _e2_action_search_compare);
	rt->dialog = search_dialog = gtk_dialog_new_with_buttons (_("Search actions"),
		GTK_WINDOW (app.main_window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		_("_Close"), GTK_RESPONSE_CLOSE, NULL);
	e2_dialog_setup (rt->dialog, app.main_window);
	gtk_widget_set_name (rt->dialog, "action-search");
	gtk_window_set_position (GTK_WINDOW (rt->dialog), GTK_WIN_POS_CENTER_ON_PARENT);
	gtk_window_set_default_size (GTK_WINDOW (rt->dialog), 620, 470);
	GtkWidget *box =
#ifdef USE_GTK2_14
		gtk_dialog_get_content_area (GTK_DIALOG (rt->dialog));
#else
		GTK_DIALOG (rt->dialog)->vbox;
#endif
	gtk_container_set_border_width (GTK_CONTAINER (box), 12);
	gtk_box_set_spacing (GTK_BOX (box), 10);
	gchar *context = g_strdup_printf (_("Active context: Pane %d"), curr_pane == &app.pane1 ? 1 : 2);
	_e2_action_search_text (box, context);
	g_free (context);
	rt->entry = gtk_entry_new ();
	atk_object_set_name (gtk_widget_get_accessible (rt->entry), _("Search actions"));
	gtk_widget_set_name (rt->entry, "action-search-entry");
	gtk_box_pack_start (GTK_BOX (box), rt->entry, FALSE, FALSE, 0);
#ifdef USE_GTK2_16
	gtk_entry_set_icon_from_stock (GTK_ENTRY (rt->entry), GTK_ENTRY_ICON_SECONDARY, GTK_STOCK_CLEAR);
	gtk_entry_set_icon_tooltip_text (GTK_ENTRY (rt->entry), GTK_ENTRY_ICON_SECONDARY, _("Clear search"));
	g_signal_connect (rt->entry, "icon-release", G_CALLBACK (_e2_action_search_clear), NULL);
#endif
	rt->store = gtk_list_store_new (N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING,
		G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_POINTER);
	rt->tree = gtk_tree_view_new_with_model (GTK_TREE_MODEL (rt->store));
	gtk_widget_set_name (rt->tree, "action-search-results");
	gtk_tree_view_set_enable_search (GTK_TREE_VIEW (rt->tree), FALSE);
	const gchar *titles[] = {_("Action"), _("Category"), _("Shortcut")};
	gint i;
	for (i = 0; i < 3; i++)
	{
		GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
		GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes (titles[i],
			renderer, "text", i, "sensitive", AVAILABLE, NULL);
		if (i == 0)
		{
			g_object_set (renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
			gtk_tree_view_column_set_expand (column, TRUE);
		}
		gtk_tree_view_append_column (GTK_TREE_VIEW (rt->tree), column);
	}
	GtkWidget *scroller = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroller), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scroller), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (scroller), rt->tree);
	gtk_box_pack_start (GTK_BOX (box), scroller, TRUE, TRUE, 0);
	rt->empty = _e2_action_search_text (box, _("No matching actions"));
	rt->summary = _e2_action_search_text (box, "");
	rt->reason = _e2_action_search_text (box, "");
	_e2_action_search_text (box, _("↑ / ↓: select action    Enter: run action    Esc: close"));
	g_signal_connect (rt->entry, "changed", G_CALLBACK (_e2_action_search_filter), rt);
	g_signal_connect (rt->entry, "activate", G_CALLBACK (_e2_action_search_activate), rt);
	g_signal_connect (rt->tree, "row-activated", G_CALLBACK (_e2_action_search_row), rt);
	g_signal_connect (gtk_tree_view_get_selection (GTK_TREE_VIEW (rt->tree)), "changed",
		G_CALLBACK (_e2_action_search_selection_changed), rt);
	g_signal_connect (rt->dialog, "key-press-event", G_CALLBACK (_e2_action_search_key), rt);
	g_signal_connect (rt->dialog, "response", G_CALLBACK (_e2_action_search_response), NULL);
	g_signal_connect (rt->dialog, "destroy", G_CALLBACK (_e2_action_search_destroy), rt);
	rt->pane_selection = g_object_ref (rt->pane->view.selection);
	rt->selection_handler = g_signal_connect (rt->pane_selection, "changed",
		G_CALLBACK (_e2_action_search_pane_selection), rt);
	gtk_widget_show_all (rt->dialog);
	_e2_action_search_filter (NULL, rt);
	gtk_widget_grab_focus (rt->entry);
	return TRUE;
}

void e2_action_search_register (void)
{
	E2_Action action = {g_strdup ("actions.search"), _e2_action_search_show,
		FALSE, E2_ACTION_TYPE_ITEM, 0, NULL, NULL};
	e2_action_register (&action);
}
