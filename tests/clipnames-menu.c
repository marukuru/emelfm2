/* Exercise clipboard variants through the real file/folder Plugins submenu. */
#include "emelfm2.h"
#include "e2_action.h"
#include "e2_context_menu.h"
#include "e2_filestore.h"
#include "e2_option.h"
#include "e2_pane.h"
#include "e2_plugins.h"

static GtkWidget *menu;
static gchar *expected;
static guint step;
static const gchar *labels[] = {
	"My copied names", "Copy full paths", "Copy full paths (one per line)"
};

static gboolean has_label (GtkWidget *widget, const gchar *text)
{
	if (GTK_IS_LABEL (widget))
		return !g_strcmp0 (gtk_label_get_text (GTK_LABEL (widget)), text);
	if (!GTK_IS_CONTAINER (widget)) return FALSE;
	GList *children = gtk_container_get_children (GTK_CONTAINER (widget));
	gboolean found = FALSE;
	for (GList *p = children; p != NULL && !found; p = p->next)
		found = has_label (p->data, text);
	g_list_free (children);
	return found;
}

static GtkWidget *item (GtkWidget *parent, const gchar *label)
{
	GList *children = gtk_container_get_children (GTK_CONTAINER (parent));
	GtkWidget *found = NULL;
	for (GList *p = children; p != NULL && found == NULL; p = p->next)
		if (has_label (p->data, label)) found = p->data;
	g_list_free (children);
	return found;
}

static GtkWidget *open_menu (void)
{
	e2_context_menu_show (0, GDK_CURRENT_TIME, 0, curr_view);
	menu = app.context_menu;
	g_assert_true (GTK_IS_MENU (menu));
	/* This test keeps the menu until the deferred action has completed. */
	g_signal_handlers_disconnect_matched (menu, G_SIGNAL_MATCH_ID,
		g_signal_lookup ("selection-done", GTK_TYPE_MENU_SHELL), 0, NULL, NULL, NULL);
	GtkWidget *plugins = item (menu, "Plugins");
	g_assert_nonnull (plugins);
	return gtk_menu_item_get_submenu (GTK_MENU_ITEM (plugins));
}

static void close_menu (void)
{
	gtk_menu_popdown (GTK_MENU (menu));
	gtk_widget_destroy (menu);
	app.context_menu = menu = NULL;
}

static void select_name (const gchar *name, gboolean clear)
{
	GtkTreeIter iter;
	gboolean found = FALSE;
	g_assert_true (gtk_tree_model_get_iter_first (curr_view->model, &iter));
	do
	{
		FileInfo *info;
		gtk_tree_model_get (curr_view->model, &iter, FINFO, &info, -1);
		if (!strcmp (info->filename, name)) { found = TRUE; break; }
	} while (gtk_tree_model_iter_next (curr_view->model, &iter));
	g_assert_true (found);
	GtkTreePath *path = gtk_tree_model_get_path (curr_view->model, &iter);
	if (clear) gtk_tree_view_set_cursor (GTK_TREE_VIEW (curr_view->treeview), path, NULL, FALSE);
	else gtk_tree_selection_select_path (curr_view->selection, path);
	gtk_tree_path_free (path);
}

static void finish (void)
{
	g_print ("clipnames menu checks passed\n");
	OPENBGL
	e2_action_run_simple_from ("command.quit", NULL, app.main_window);
}

static gboolean tick (gpointer unused)
{
	if (curr_view == NULL || !app.pane1.view.dir[0] || !app.pane2.view.dir[0]
		|| g_atomic_int_get (&app.pane1.view.listcontrols.cd_working)
		|| g_atomic_int_get (&app.pane2.view.listcontrols.cd_working)
		|| g_atomic_int_get (&app.pane1.view.listcontrols.refresh_working)
		|| g_atomic_int_get (&app.pane2.view.listcontrols.refresh_working)) return TRUE;
	CLOSEBGL
	const gchar *testcase = g_getenv ("E2_CLIPNAMES_CASE");
	if (!strcmp (testcase, "setup"))
	{
		GtkTreeStore *store = GTK_TREE_STORE (e2_option_get ("plugins")->ex.tree.model);
		gtk_tree_store_clear (store);
		GtkTreeIter iter;
		gtk_tree_store_append (store, &iter, NULL);
		gtk_tree_store_set (store, &iter, LOAD_COL, TRUE, MENU_COL, TRUE,
			LABEL_COL, labels[0], ICON_COL, "", TIP_COL, "Copy selected names",
			FILE_COL, "e2p_clipnames.so", PATH_COL, g_getenv ("E2_CLIPNAMES_PLUGIN_DIR"),
			SIG_COL, "clipnames", -1);
		finish ();
		return FALSE;
	}
	if (strcmp (testcase, "enabled"))
	{
		select_name ("a file.txt", TRUE);
		GtkWidget *plugins = open_menu ();
		for (guint i = 0; i < G_N_ELEMENTS (labels); i++)
			g_assert_null (item (plugins, labels[i]));
		g_assert_cmpint (e2_action_get ("file.copy_name") != NULL, ==,
			!strcmp (testcase, "hidden"));
		close_menu ();
		finish ();
		return FALSE;
	}
	g_assert_nonnull (e2_action_get ("file.copy_name"));
	if (expected != NULL)
	{
		gchar *actual = gtk_clipboard_wait_for_text (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD));
		if (!g_strcmp0 (actual, "unchanged"))
		{
			g_free (actual);
			OPENBGL
			return TRUE; //the menu action is deferred until GTK finishes activation
		}
		g_assert_cmpstr (actual, ==, expected);
		g_free (actual);
		g_free (expected);
		expected = NULL;
		close_menu ();
		step++;
	}
	if (step == 6)
	{
		gtk_tree_selection_unselect_all (curr_view->selection);
		GtkWidget *plugins = open_menu ();
		for (guint i = 0; i < G_N_ELEMENTS (labels); i++)
			g_assert_null (item (plugins, labels[i]));
		close_menu ();
		finish ();
		return FALSE;
	}
	if (step == 5) e2_pane_activate_other ();
	const gchar *name = step == 4 ? "folder name" : "a file.txt";
	select_name (name, TRUE);
	gboolean multiple = step == 2 || step == 3;
	if (multiple) select_name ("b-日本語.txt", FALSE);
	GtkWidget *plugins = open_menu ();
	for (guint i = 0; i < G_N_ELEMENTS (labels); i++)
		g_assert_nonnull (item (plugins, labels[i]));
	/* The variants should be adjacent, following the user's customised label. */
	GList *children = gtk_container_get_children (GTK_CONTAINER (plugins));
	GList *first = g_list_find (children, item (plugins, labels[0]));
	g_assert_nonnull (first);
	g_assert_true (first->next->data == item (plugins, labels[1]));
	g_assert_true (first->next->next->data == item (plugins, labels[2]));
	g_list_free (children);
	guint variant = step == 0 ? 0 : (step == 2 ? 2 : 1);
	if (variant == 0) expected = g_strdup (name);
	else if (multiple)
		expected = g_strdup_printf ("%s%s%c%sb-日本語.txt", curr_view->dir, name,
			variant == 2 ? '\n' : ' ', curr_view->dir);
	else expected = g_strconcat (curr_view->dir, name, NULL);
	gtk_clipboard_set_text (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD), "unchanged", -1);
	gtk_menu_item_activate (GTK_MENU_ITEM (item (plugins, labels[variant])));
	gtk_menu_popdown (GTK_MENU (menu));
	OPENBGL
	return TRUE;
}

__attribute__((constructor)) static void schedule (void)
{
	unsetenv ("LD_PRELOAD");
	g_timeout_add (150, tick, NULL);
}
