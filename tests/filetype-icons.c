/* Exercise persisted filetype icons through the real configuration and menus. */
#include "emelfm2.h"
#include "e2_config_dialog.h"
#include "e2_dialog.h"
#include "e2_filetype.h"
#include "e2_filestore.h"
#include "e2_menu.h"
#include "e2_option_tree.h"
#include "e2_select_image_dialog.h"

static guint step;
static E2_OptionSet *set;
static E2_SpecificConfDialogRuntime *config;
static E2_SID_Runtime *picker;
static gchar *custom_icon;
static GtkWidget *editor;

static void check_svg_pixels (GdkPixbuf *actual)
{
    g_assert_nonnull (actual);
    GdkPixbuf *expected = gdk_pixbuf_new_from_file_at_scale (custom_icon,
        gdk_pixbuf_get_width (actual), gdk_pixbuf_get_height (actual), TRUE, NULL);
    g_assert_nonnull (expected);
    g_assert_cmpint (gdk_pixbuf_get_width (actual), ==, gdk_pixbuf_get_width (expected));
    g_assert_cmpint (gdk_pixbuf_get_height (actual), ==, gdk_pixbuf_get_height (expected));
    gint channels = gdk_pixbuf_get_n_channels (actual);
    g_assert_cmpint (channels, ==, gdk_pixbuf_get_n_channels (expected));
    for (gint y = 0; y < gdk_pixbuf_get_height (actual); y++)
        g_assert_cmpint (memcmp (gdk_pixbuf_get_pixels (actual) + y * gdk_pixbuf_get_rowstride (actual),
            gdk_pixbuf_get_pixels (expected) + y * gdk_pixbuf_get_rowstride (expected),
            channels * gdk_pixbuf_get_width (actual)), ==, 0);
    g_object_unref (expected);
}

static void check_config_svg (const gchar *path)
{
    GtkTreeIter iter;
    g_assert_true (gtk_tree_model_get_iter_from_string (set->ex.tree.model, &iter, path));
    GtkTreeViewColumn *column = gtk_tree_view_get_column (GTK_TREE_VIEW (set->widget), 3);
    gtk_tree_view_column_cell_set_cell_data (column, set->ex.tree.model, &iter, FALSE, FALSE);
    GList *cells = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (column));
    GdkPixbuf *pixbuf;
    g_object_get (cells->data, "pixbuf", &pixbuf, NULL);
    check_svg_pixels (pixbuf);
    g_object_unref (pixbuf);
    g_list_free (cells);
}

static GtkWidget *find_widget (GtkWidget *widget, GType type)
{
    if (G_TYPE_CHECK_INSTANCE_TYPE (widget, type)) return widget;
    if (!GTK_IS_CONTAINER (widget)) return NULL;
    GList *children = gtk_container_get_children (GTK_CONTAINER (widget));
    GtkWidget *found = NULL;
    for (GList *p = children; p != NULL && found == NULL; p = p->next)
        found = find_widget (p->data, type);
    g_list_free (children);
    return found;
}

static GtkWidget *find_window (const gchar *title)
{
    GList *windows = gtk_window_list_toplevels ();
    GtkWidget *found = NULL;
    for (GList *p = windows; p != NULL; p = p->next)
        if (gtk_window_get_title (p->data) != NULL
            && strstr (gtk_window_get_title (p->data), title) != NULL)
        { found = p->data; break; }
    g_list_free (windows);
    g_assert_nonnull (found);
    return found;
}

static void check_cell (const gchar *path, const gchar *expected)
{
    GtkTreeIter iter;
    gchar *icon;
    g_assert_true (gtk_tree_model_get_iter_from_string (set->ex.tree.model, &iter, path));
    gtk_tree_model_get (set->ex.tree.model, &iter, 3, &icon, -1);
    g_assert_cmpstr (icon, ==, expected);
    g_free (icon);
}

static void check_runtime (gboolean reordered)
{
    const gchar **actions = e2_filetype_get_actions ("iconprobe");
    g_assert_nonnull (actions);
    guint stock = reordered ? 1 : 0, plain = reordered ? 0 : 1;
    g_assert_cmpstr (actions[stock], ==, "Stock@echo probe %f");
    g_assert_cmpstr (e2_filetype_get_action_icon (actions[stock]), ==,
        reordered ? "gtk-copy" : NULL);
    g_assert_cmpstr (actions[plain], ==, "Plain@echo probe %f");
    g_assert_null (e2_filetype_get_action_icon (actions[plain]));
    g_assert_cmpstr (e2_filetype_get_action_icon (actions[2]), ==, custom_icon);
    g_assert_cmpstr (e2_filetype_get_action_icon (actions[3]), ==, "gtk-paste");
    g_assert_null (actions[4]);
    gchar *command = e2_filetype_get_default_action ("iconprobe");
    g_assert_cmpstr (command, ==, actions[0]);
    g_free (command);
    /* Equal action strings must not share icon settings across categories. */
    const gchar **other = e2_filetype_get_actions ("otherprobe");
    g_assert_cmpstr (other[0], ==, actions[stock]);
    g_assert_cmpstr (e2_filetype_get_action_icon (other[0]), ==, "gtk-open");
    e2_option_bool_set ("anycase-filetypes", TRUE);
    g_assert_true (e2_filetype_get_actions ("ICONPROBE") == actions);
}

static void check_menu (gboolean reordered, gboolean icons_enabled)
{
    e2_option_set_from_string ("menu-show-icons", icons_enabled ? "yes" : "no");
    GtkWidget *menu = e2_menu_get ();
    e2_menu_add_filetype_items (menu, curr_view);
    GList *children = gtk_container_get_children (GTK_CONTAINER (menu));
    guint row = 0;
    for (GList *p = children; p != NULL && row < 4; p = p->next)
    {
        GtkWidget *item = p->data;
        const gchar *command = g_object_get_data (G_OBJECT (item), "action-cmd-key");
        if (command == NULL) continue;
        g_assert_cmpstr (command, ==, "echo probe %f");
        gboolean expected = icons_enabled && (row >= 2 || (reordered && row == 1));
        GtkWidget *image = find_widget (item, GTK_TYPE_IMAGE);
#ifndef USE_GTK3_10
        if (GTK_IS_IMAGE_MENU_ITEM (item))
            image = gtk_image_menu_item_get_image (GTK_IMAGE_MENU_ITEM (item));
#endif
        g_assert_cmpint (image != NULL, ==, expected);
        if (image != NULL)
            g_assert_cmpint (gtk_image_get_storage_type (GTK_IMAGE (image)), !=, GTK_IMAGE_EMPTY);
        if (row == 2 && icons_enabled)
            check_svg_pixels (gtk_image_get_pixbuf (GTK_IMAGE (image)));
        row++;
    }
    g_assert_cmpuint (row, ==, 4);
    g_list_free (children);
    gtk_widget_destroy (menu);
}

static void select_file (void)
{
    GtkTreeIter iter;
    gint row = 0;
    g_assert_true (gtk_tree_model_get_iter_first (curr_view->model, &iter));
    do
    {
        FileInfo *info;
        gtk_tree_model_get (curr_view->model, &iter, FINFO, &info, -1);
        if (!strcmp (info->filename, "a.iconprobe"))
        {
            e2_fileview_focus_row (curr_view, row, TRUE, TRUE, FALSE, FALSE);
            return;
        }
        row++;
    } while (gtk_tree_model_iter_next (curr_view->model, &iter));
    g_assert_not_reached ();
}

static void select_stock (const gchar *name)
{
    GtkTreeIter iter;
    g_assert_true (gtk_tree_model_get_iter_first (picker->stockmodel, &iter));
    do
    {
        gchar *label;
        gtk_tree_model_get (picker->stockmodel, &iter, 1, &label, -1);
        gboolean match = !strcmp (label, name);
        g_free (label);
        if (!match) continue;
        gtk_notebook_set_current_page (picker->notebook, 1);
        GtkTreePath *path = gtk_tree_model_get_path (picker->stockmodel, &iter);
        gtk_icon_view_select_path (picker->stockview, path);
        gtk_tree_path_free (path);
        gtk_dialog_response (GTK_DIALOG (picker->dialog), E2_RESPONSE_MORE);
        return;
    } while (gtk_tree_model_iter_next (picker->stockmodel, &iter));
    g_assert_not_reached ();
}

static void browse_svg (void)
{
    gchar *directory = g_path_get_dirname (custom_icon);
    g_assert_true (gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (picker->dir_chooser), directory));
    g_free (directory);
}

static void select_svg (void)
{
    GtkTreeIter iter;
    g_assert_true (gtk_tree_model_get_iter_first (picker->custommodel, &iter));
    do
    {
        gchar *filename;
        gtk_tree_model_get (picker->custommodel, &iter, 2, &filename, -1);
        gboolean match = !strcmp (filename, custom_icon);
        g_free (filename);
        if (!match) continue;
        gtk_notebook_set_current_page (picker->notebook, 0);
        GtkTreePath *path = gtk_tree_model_get_path (picker->custommodel, &iter);
        gtk_icon_view_select_path (picker->customview, path);
        gtk_tree_path_free (path);
        gtk_dialog_response (GTK_DIALOG (picker->dialog), E2_RESPONSE_MORE);
        g_assert_cmpstr (g_object_get_data (G_OBJECT (picker->dialog), "image"), ==, custom_icon);
        return;
    } while (gtk_tree_model_iter_next (picker->custommodel, &iter));
    g_assert_not_reached ();
}

static GtkTreeView *command_view (GtkWidget *widget)
{
    if (GTK_IS_TREE_VIEW (widget))
    {
        GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));
        if (gtk_tree_model_get_n_columns (model) == 3) return GTK_TREE_VIEW (widget);
    }
    if (!GTK_IS_CONTAINER (widget)) return NULL;
    GList *children = gtk_container_get_children (GTK_CONTAINER (widget));
    GtkTreeView *found = NULL;
    for (GList *p = children; p != NULL && found == NULL; p = p->next)
        found = command_view (p->data);
    g_list_free (children);
    return found;
}

static void click_icon (GtkTreeView *tree, const gchar *path_string, gint column_index)
{
    GtkTreeViewColumn *column = gtk_tree_view_get_column (tree, column_index);
    g_assert_nonnull (column);
    g_assert_cmpstr (gtk_tree_view_column_get_title (column), ==, "Icon");
    GtkTreePath *path = gtk_tree_path_new_from_string (path_string);
    GdkRectangle area;
    gtk_tree_view_get_cell_area (tree, path, column, &area);
    gtk_tree_path_free (path);
    GdkEventButton event = { 0 };
    event.type = GDK_BUTTON_PRESS;
    event.window = gtk_tree_view_get_bin_window (tree);
    event.button = 1;
    event.x = area.x + area.width / 2;
    event.y = area.y + area.height / 2;
    gboolean handled = FALSE;
    g_signal_emit_by_name (tree, "button-press-event", &event, &handled);
    g_assert_true (handled);
}

static void check_editor_icon (const gchar *path, const gchar *expected)
{
    GtkTreeView *view = command_view (editor);
    GtkTreeModel *model = gtk_tree_view_get_model (view);
    GtkTreeIter iter;
    gchar *icon;
    g_assert_true (gtk_tree_model_get_iter_from_string (model, &iter, path));
    gtk_tree_model_get (model, &iter, 2, &icon, -1);
    g_assert_cmpstr (icon, ==, expected);
    g_free (icon);
    /* Verify the actual preview is populated or cleared along with the value. */
    GtkTreeViewColumn *column = gtk_tree_view_get_column (view, 2);
    gtk_tree_view_column_cell_set_cell_data (column, model, &iter, FALSE, FALSE);
    GList *cells = gtk_cell_layout_get_cells (GTK_CELL_LAYOUT (column));
    GdkPixbuf *pixbuf;
    g_object_get (cells->data, "pixbuf", &pixbuf, NULL);
    g_assert_cmpint (pixbuf != NULL, ==, expected != NULL && *expected != '\0');
    if (expected != NULL && !strcmp (expected, custom_icon)) check_svg_pixels (pixbuf);
    if (pixbuf != NULL) g_object_unref (pixbuf);
    g_list_free (cells);
}

static gboolean tick (gpointer unused)
{
    if (curr_view == NULL || curr_view->dir[0] == '\0'
        || g_atomic_int_get (&app.pane1.view.listcontrols.cd_working)
        || g_atomic_int_get (&app.pane2.view.listcontrols.cd_working)) return TRUE;
    CLOSEBGL
    switch (step++)
    {
        case 0:
        {
            set = e2_option_get_simple ("filetypes");
            custom_icon = g_build_filename (g_getenv ("E2_FILETYPE_ICON_TEST"), "external icons", "move.svg", NULL);
            select_file ();
            if (g_getenv ("E2_FILETYPE_ICON_RELOAD") != NULL)
            {
                check_runtime (TRUE);
                check_menu (TRUE, TRUE);
                check_menu (TRUE, FALSE);
                g_print ("filetype icon reload passed\n");
                e2_main_closedown (TRUE, TRUE, TRUE);
                return FALSE;
            }
            /* Mix legacy three-field rows, a custom file, and an unnamed command. */
            gchar *fixture = g_strdup_printf (
                "filetypes=<\nIcon regression||\n\t|extensions|\n\t\t|iconprobe|\n"
                "\t|commands|\n\t\t|Stock|echo probe %%f\n\t\t|Plain|echo probe %%f|\n"
                "\t\t|Custom|echo probe %%f|%s\n\t\t|echo probe %%f||gtk-paste\n"
                "Other icons||\n\t|extensions|\n\t\t|otherprobe|\n"
                "\t|commands|\n\t\t|Stock|echo probe %%f|gtk-open\n>\n", custom_icon);
            gchar **lines = g_strsplit (fixture, "\n", -1);
            gtk_tree_store_clear (GTK_TREE_STORE (set->ex.tree.model));
            g_assert_true (e2_option_tree_set_from_array ("filetypes", lines, NULL, NULL));
            g_strfreev (lines);
            g_free (fixture);
            check_cell ("0:1:0", "");
            e2_filetype_apply_allnew ();
            check_runtime (FALSE);
            check_menu (FALSE, TRUE);
            config = e2_config_dialog_single ("filetypes", e2_filetype_apply_allnew, TRUE);
            g_assert_nonnull (config);
            GtkTreeView *tree = GTK_TREE_VIEW (set->widget);
            g_assert_cmpstr (gtk_tree_view_column_get_title (gtk_tree_view_get_column (tree, 3)), ==, "Icon");
            gtk_tree_view_expand_all (tree);
            GtkTreePath *path = gtk_tree_path_new_from_string ("0:1:0");
            gtk_tree_view_set_cursor (tree, path, gtk_tree_view_get_column (tree, 3), FALSE);
            gtk_tree_view_scroll_to_cell (tree, path, NULL, TRUE, 0.5, 0.5);
            gtk_tree_path_free (path);
            break;
        }
        case 1:
        {
            click_icon (GTK_TREE_VIEW (set->widget), "0:1:0", 3);
            picker = g_object_get_data (G_OBJECT (config->dialog), "select icon for filetypes");
            g_assert_nonnull (picker);
            break;
        }
        case 2:
            select_stock ("copy");
            check_cell ("0:1:0", "gtk-copy");
            g_assert_true ((set->ex.tree.flags & E2_OPTION_TREE_SET_EDITED) != 0);
            gtk_dialog_response (GTK_DIALOG (picker->dialog), E2_RESPONSE_REMOVE);
            check_cell ("0:1:0", "");
            e2_filetype_apply_allnew ();
            check_menu (FALSE, TRUE);
            select_stock ("copy");
            browse_svg ();
            break;
        case 3:
            select_svg ();
            check_cell ("0:1:0", custom_icon);
            check_config_svg ("0:1:0");
            select_stock ("copy");
            gtk_dialog_response (GTK_DIALOG (picker->dialog), GTK_RESPONSE_CANCEL);
            gtk_dialog_response (GTK_DIALOG (config->dialog), E2_RESPONSE_APPLY);
            /* The separate editor must carry icons along when it rewrites rows. */
            e2_filetype_config_show ("iconprobe");
            editor = find_window ("edit filetypes");
            break;
        case 4:
            check_editor_icon ("0", "gtk-copy");
            check_editor_icon ("1", "");
            check_editor_icon ("2", custom_icon);
            click_icon (command_view (editor), "0", 2);
            picker = g_object_get_data (G_OBJECT (editor), "select icon for filetypes");
            g_assert_nonnull (picker);
            browse_svg ();
            break;
        case 5:
        {
            select_svg ();
            check_editor_icon ("0", custom_icon);
            select_stock ("cut");
            check_editor_icon ("0", "gtk-cut");
            check_cell ("0:1:0", "gtk-copy"); /* Pending until editor Apply. */
            gtk_dialog_response (GTK_DIALOG (picker->dialog), E2_RESPONSE_REMOVE);
            check_editor_icon ("0", "");
            GtkTreeView *view = command_view (editor);
            GtkTreeStore *store = GTK_TREE_STORE (gtk_tree_view_get_model (view));
            GtkTreeIter first, second;
            g_assert_true (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (store), &first));
            second = first;
            g_assert_true (gtk_tree_model_iter_next (GTK_TREE_MODEL (store), &second));
            gtk_tree_store_swap (store, &first, &second);
            gtk_tree_selection_select_all (gtk_tree_view_get_selection (view));
            select_stock ("copy"); /* Still targets the original row after moving. */
            check_editor_icon ("0", "");
            check_editor_icon ("1", "gtk-copy");
            gtk_dialog_response (GTK_DIALOG (picker->dialog), GTK_RESPONSE_CANCEL);
            /* New rows have a NULL icon until their first choice. */
            gtk_tree_store_append (store, &first, NULL);
            gtk_tree_store_set (store, &first, 0, "Temporary", 1, "echo probe %f", -1);
            GtkTreePath *path = gtk_tree_model_get_path (GTK_TREE_MODEL (store), &first);
            gtk_tree_view_scroll_to_cell (view, path, NULL, TRUE, 0.5, 0.5);
            gtk_tree_path_free (path);
            break;
        }
        case 6:
            click_icon (command_view (editor), "4", 2);
            picker = g_object_get_data (G_OBJECT (editor), "select icon for filetypes");
            g_assert_nonnull (picker);
            break;
        case 7:
        {
            select_stock ("paste");
            check_editor_icon ("4", "gtk-paste");
            GtkTreeStore *store = GTK_TREE_STORE (gtk_tree_view_get_model (command_view (editor)));
            GtkTreeIter temporary;
            g_assert_true (gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (store), &temporary, "4"));
            gtk_tree_store_remove (store, &temporary);
            select_stock ("cut"); /* A removed row must not redirect a pending edit. */
            check_editor_icon ("1", "gtk-copy");
            gtk_dialog_response (GTK_DIALOG (picker->dialog), GTK_RESPONSE_CANCEL);
            gtk_dialog_response (GTK_DIALOG (editor), E2_RESPONSE_APPLY);
            /* The separate editor deliberately skips unnamed commands: check the
             * reordered named entries here, then restore that legacy fixture. */
            GtkTreeIter parent, row;
            g_assert_true (gtk_tree_model_get_iter_from_string (set->ex.tree.model, &parent, "0:1"));
            gtk_tree_store_append (GTK_TREE_STORE (set->ex.tree.model), &row, &parent);
            gtk_tree_store_set (GTK_TREE_STORE (set->ex.tree.model), &row,
                0, "", 1, "echo probe %f", 2, "", 3, "gtk-paste", -1);
            e2_filetype_apply_allnew ();
            check_runtime (TRUE);
            check_menu (TRUE, TRUE);
            check_menu (TRUE, FALSE);
            e2_filetype_config_show ("iconprobe");
            editor = find_window ("edit filetypes");
            break;
        }
        case 8:
            click_icon (command_view (editor), "1", 2);
            picker = g_object_get_data (G_OBJECT (editor), "select icon for filetypes");
            g_assert_nonnull (picker);
            break;
        case 9:
            select_stock ("cut");
            check_editor_icon ("1", "gtk-cut");
            /* Discard also closes any open picker and restores saved icons. */
            gtk_dialog_response (GTK_DIALOG (editor), GTK_RESPONSE_CANCEL);
            check_runtime (TRUE);
            check_cell ("0:1:1", "gtk-copy");
            config = e2_config_dialog_single ("filetypes", e2_filetype_apply_allnew, TRUE);
            check_cell ("0:1:1", "gtk-copy");
            gtk_dialog_response (GTK_DIALOG (config->dialog), GTK_RESPONSE_CANCEL);
            g_print ("filetype editor icon picker, new rows, removal, reorder and discard passed\n");
            g_print ("filetype icon picker, removal, compatibility, menus and reorder passed\n");
            e2_main_closedown (TRUE, TRUE, TRUE);
            return FALSE;
    }
    OPENBGL
    return TRUE;
}

__attribute__((constructor)) static void schedule (void)
{ unsetenv ("LD_PRELOAD"); g_timeout_add (500, tick, NULL); }
