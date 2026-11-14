/* Exercise file activation with commands that borrow or replace macro text. */
#include "emelfm2.h"
#include "e2_filestore.h"
#include "e2_option.h"
#include "e2_utils.h"
#include <glib/gstdio.h>

static guint scenario;
static gboolean waiting;
static gchar *result_path, *expected;
static const gchar *names[] = { "ordinary.JPG", "image with spaces.JPG" };

static void check_macro_ownership (void)
{
    gchar *action = g_strdup ("Image viewer@ristretto");
    gchar *command = strchr (action, '@') + 1;
    gchar *result = e2_utils_replace_name_macros (command, "/tmp/image.JPG");
    g_assert_true (result == command);

    GPtrArray *selection = g_ptr_array_new ();
    result = e2_utils_replace_multiname (command, curr_view->dir, selection, FALSE);
    g_assert_true (result == command);
    E2_SelectedItemInfo info = { 0 };
    g_strlcpy (info.filename, names[0], sizeof (info.filename));
    g_ptr_array_add (selection, &info);
    result = e2_utils_replace_multiname (command, curr_view->dir, selection, FALSE);
    g_assert_true (result == command);
    g_ptr_array_free (selection, TRUE);
    g_free (action);
}

static GtkTreePath *path_for (const gchar *name)
{
    GtkTreeIter iter;
    g_assert_true (gtk_tree_model_get_iter_first (curr_view->model, &iter));
    do
    {
        FileInfo *info;
        gtk_tree_model_get (curr_view->model, &iter, FINFO, &info, -1);
        if (!strcmp (info->filename, name))
            return gtk_tree_model_get_path (curr_view->model, &iter);
    } while (gtk_tree_model_iter_next (curr_view->model, &iter));
    g_assert_not_reached ();
}

static gboolean tick (gpointer unused)
{
    if (app.filetypes == NULL || curr_view == NULL || curr_view->treeview == NULL
        || curr_view->listcontrols.newpath
        || g_atomic_int_get (&curr_view->listcontrols.cd_working)
        || g_atomic_int_get (&curr_view->listcontrols.refresh_working)) return TRUE;
    CLOSEBGL
    if (result_path == NULL)
    {
        check_macro_ownership ();
        result_path = g_build_filename (g_getenv ("E2_FILE_OPEN_TEST"), "result", NULL);
        e2_option_bool_set ("anycase-filetypes", TRUE);
    }
    if (waiting)
    {
        gchar *actual;
        gsize length;
        if (!g_file_get_contents (result_path, &actual, &length, NULL))
        {
            OPENBGL
            return TRUE;
        }
        /* Include the NUL terminator to verify exactly one filename argument. */
        g_assert_cmpmem (actual, length, expected, strlen (expected) + 1);
        g_free (actual);
        g_free (expected);
        g_unlink (result_path);
        waiting = FALSE;
        scenario++;
    }
    if (scenario == 8)
    {
        fprintf (stderr, "file activation: all 8 commands received the image path\n");
        g_free (result_path);
        e2_main_closedown (TRUE, TRUE, TRUE);
        return FALSE;
    }

    /* Both a plain command and label@command must append the filename when
     * no macro is present; %f and %p must replace it exactly once. */
    const gchar *suffix[] = { "", "", " %f", " %p" };
    guint variant = scenario / G_N_ELEMENTS (names);
    gchar **actions = g_new0 (gchar *, 2);
    actions[0] = g_strdup_printf ("%s'%s/handler'%s", variant == 0 ? "" : "Image viewer@",
        g_getenv ("E2_FILE_OPEN_TEST"), suffix[variant]);
    g_hash_table_insert (app.filetypes, g_strdup ("jpg"), actions);
    const gchar *name = names[scenario % G_N_ELEMENTS (names)];
    expected = g_build_filename (curr_view->dir, name, NULL);
    GtkTreePath *path = path_for (name);
    e2_fileview_focus_row (curr_view, *gtk_tree_path_get_indices (path),
        TRUE, TRUE, FALSE, FALSE);
    waiting = TRUE;
    /* This signal is shared by double-click and Enter. */
    g_signal_emit_by_name (curr_view->treeview, "row-activated", path, NULL);
    gtk_tree_path_free (path);
    OPENBGL
    return TRUE;
}

__attribute__((constructor)) static void schedule (void)
{ unsetenv ("LD_PRELOAD"); g_timeout_add (100, tick, NULL); }
