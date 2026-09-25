/* Exercise file selection and command dispatch through real directory menus. */
#include "emelfm2.h"
#include "e2_filestore.h"
#include "e2_option.h"
#include "e2_pane.h"
#include <glib/gstdio.h>

static guint scenario;
static gboolean waiting;
static gchar *result_path, *expected;
static const gchar *names[] = { "a.ctxtxt", "b.ctximg", "c.ctximg" };

static gint row_for (ViewInfo *view, const gchar *name)
{
    GtkTreeIter iter;
    gint row = 0;
    g_assert_true (gtk_tree_model_get_iter_first (view->model, &iter));
    do
    {
        FileInfo *info;
        gtk_tree_model_get (view->model, &iter, FINFO, &info, -1);
        if (!strcmp (info->filename, name)) return row;
        row++;
    } while (gtk_tree_model_iter_next (view->model, &iter));
    g_assert_not_reached ();
}

static void select_file (ViewInfo *view, guint index, gboolean clear)
{
    e2_fileview_focus_row (view, row_for (view, names[index]), TRUE, clear, FALSE, FALSE);
}

static GtkWidget *menu_item (GtkWidget *menu, const gchar *text)
{
    GList *items = gtk_container_get_children (GTK_CONTAINER (menu)), *link;
    GtkWidget *found = NULL;
    for (link = items; link != NULL; link = link->next)
    {
        GtkWidget *child = gtk_bin_get_child (GTK_BIN (link->data));
        if (GTK_IS_LABEL (child) && !strcmp (gtk_label_get_text (GTK_LABEL (child)), text))
        {
            found = link->data;
            break;
        }
    }
    g_list_free (items);
    return found;
}

static void right_click (ViewInfo *view, gboolean blank)
{
    GtkTreeView *tree = GTK_TREE_VIEW (view->treeview);
    GtkTreePath *path = gtk_tree_path_new_from_indices (row_for (view,
        names[blank ? 2 : 1]), -1);
    GdkRectangle area;
    gtk_tree_view_get_background_area (tree, path, NULL, &area);
    gtk_tree_path_free (path);
    GdkEvent *event = gdk_event_new (GDK_BUTTON_PRESS);
    event->button.window = g_object_ref (gtk_tree_view_get_bin_window (tree));
    event->button.button = 3;
    event->button.time = GDK_CURRENT_TIME;
    event->button.x = 20;
    event->button.y = area.y + (blank ? area.height + 10 : area.height / 2);
#ifdef USE_GTK3_0
    GdkDeviceManager *manager = gdk_display_get_device_manager (gtk_widget_get_display (view->treeview));
    gdk_event_set_device (event, gdk_device_manager_get_client_pointer (manager));
#else
    event->button.device = gdk_display_get_core_pointer (gtk_widget_get_display (view->treeview));
#endif
    gtk_main_do_event (event);
    gdk_event_free (event);
    g_assert_true (GTK_IS_MENU (app.context_menu));
    g_assert_true (curr_view == view);
    g_assert_true (gtk_window_get_focus (GTK_WINDOW (app.main_window)) == view->treeview);
}

static void setup (void)
{
    const gchar *root = g_getenv ("E2_DIRECTORY_CONTEXT_TEST");
    result_path = g_build_filename (root, "result", NULL);
    const gchar *extensions[] = { "ctxtxt", "ctximg" };
    const gchar *labels[] = { "Text probe", "Image probe" };
    const gchar *types[] = { "text", "image" };
    guint i;
    for (i = 0; i < G_N_ELEMENTS (extensions); i++)
    {
        gchar **actions = g_new0 (gchar *, 2);
        actions[0] = g_strdup_printf ("%s@'%s/handler' %s %%f", labels[i], root, types[i]);
        g_hash_table_insert (app.filetypes, g_strdup (extensions[i]), actions);
    }
    /* A user command exercises the generic menu even with an empty selection. */
    E2_OptionSet *set = e2_option_get ("context-menu");
    GtkTreeIter iter;
    gchar *command = g_strdup_printf ("'%s/handler'", root);
    gtk_tree_store_append (set->ex.tree.model, &iter, NULL);
    gtk_tree_store_set (set->ex.tree.model, &iter, 0, "Directory probe", 1, "",
        2, FALSE, 3, FALSE, 4, command, 5, "directory", -1);
    g_free (command);
}

static gboolean ready (ViewInfo *view)
{
    return view->treeview != NULL && !view->listcontrols.newpath
        && !g_atomic_int_get (&view->listcontrols.cd_working)
        && !g_atomic_int_get (&view->listcontrols.refresh_working);
}

static gboolean tick (gpointer data)
{
    if (!ready (&app.pane1.view) || !ready (&app.pane2.view)) return TRUE;
    CLOSEBGL
    if (result_path == NULL) setup ();
    if (waiting)
    {
        gchar *actual = NULL;
        if (!g_file_get_contents (result_path, &actual, NULL, NULL))
        {
            OPENBGL
            return TRUE;
        }
        g_assert_cmpstr (actual, ==, expected);
        g_free (actual);
        g_free (expected);
        g_unlink (result_path);
        fprintf (stderr, "directory context: scenario %u passed\n", scenario++);
        waiting = FALSE;
    }
    if (scenario == 28)
    {
        gchar *done = g_build_filename (g_getenv ("E2_DIRECTORY_CONTEXT_TEST"), "passed", NULL);
        g_assert_true (g_file_set_contents (done, "passed", -1, NULL));
        g_free (done);
        e2_main_closedown (TRUE, TRUE, TRUE);
        return FALSE;
    }

    ViewInfo *target = (scenario % 2 == 0) ? &app.pane1.view : &app.pane2.view;
    ViewInfo *opposite = (scenario % 2 == 0) ? &app.pane2.view : &app.pane1.view;
    gboolean blank = scenario >= 16 && scenario < 24;
    gboolean output_focus = scenario == 24 || scenario == 25;
    gboolean mixed = scenario >= 26;
    gboolean active = (scenario >= 12 && scenario < 16) || output_focus;
    gboolean windows = scenario < 12 ? (scenario / 6 != 0) :
        (scenario < 16 ? scenario >= 14 : blank);
    gboolean extra = scenario >= 20;
    guint selection = scenario < 12 ? (scenario / 2) % 3 : (mixed ? 2 : 0);
    e2_option_bool_set ("windows-right-click", windows);
    e2_option_bool_set ("windows-right-click-extra", extra);

    /* Leave a text selection in the target, no selection, or two image files. */
    select_file (target, 0, TRUE);
    if (selection == 1 || (blank && scenario % 4 >= 2))
        gtk_tree_selection_unselect_all (target->selection);
    else if (selection == 2)
    {
        select_file (target, mixed ? 0 : 1, TRUE);
        select_file (target, mixed ? 1 : 2, FALSE);
    }
    select_file (opposite, 0, TRUE);
    ViewInfo *focused = active ? target : opposite;
    if (curr_view != focused) e2_pane_activate_other ();
    gtk_widget_grab_focus (focused->treeview);
    if (output_focus) gtk_widget_grab_focus (GTK_WIDGET (app.tab.text));
    right_click (target, blank);

    gboolean text = (active && !windows && !output_focus) || mixed;
    gint count = blank ? (extra && scenario % 4 < 2 ? 1 : 0) : (selection == 2 ? 2 : 1);
    g_assert_cmpint (gtk_tree_selection_count_selected_rows (target->selection), ==, count);
    g_assert_cmpint (gtk_tree_selection_count_selected_rows (opposite->selection), ==, 1);
    g_assert_cmpstr (e2_fileview_get_selected_first_local (opposite, TRUE)->filename, ==, names[0]);
    if (!blank)
    {
        g_assert_cmpstr (e2_fileview_get_selected_first_local (target, TRUE)->filename,
            ==, names[text ? 0 : 1]);
        GtkTreePath *cursor;
        gtk_tree_view_get_cursor (GTK_TREE_VIEW (target->treeview), &cursor, NULL);
        g_assert_nonnull (cursor);
        g_assert_cmpint (*gtk_tree_path_get_indices (cursor), ==, row_for (target, names[text && !mixed ? 0 : 1]));
        g_assert_cmpint (target->row, ==, *gtk_tree_path_get_indices (cursor));
        gtk_tree_path_free (cursor);
    }
    GtkWidget *item = menu_item (app.context_menu,
        blank ? "Directory probe" : (text ? "Text probe" : "Image probe"));
    g_assert_nonnull (item);
    if (!blank) g_assert_null (menu_item (app.context_menu, text ? "Image probe" : "Text probe"));
    gchar *dir = g_strndup (target->dir, strlen (target->dir) - 1);
    expected = g_strdup_printf ("%s\n%s\n%s", dir,
        blank ? "directory" : (text ? "text" : "image"),
        blank ? "" : (mixed ? "a.ctxtxt\nb.ctximg\n" :
            (text ? "a.ctxtxt\n" : (selection == 2 ? "b.ctximg\nc.ctximg\n" : "b.ctximg\n"))));
    g_free (dir);
    GtkWidget *menu = app.context_menu;
    gtk_menu_item_activate (GTK_MENU_ITEM (item));
    gtk_menu_popdown (GTK_MENU (menu));
    g_signal_emit_by_name (menu, "selection-done");
    waiting = TRUE;
    OPENBGL
    return TRUE;
}

__attribute__((constructor)) static void schedule (void)
{ unsetenv ("LD_PRELOAD"); g_timeout_add (200, tick, NULL); }
