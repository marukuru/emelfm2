/* One-time command icon discovery, exercised through both editor buttons. */
#include "emelfm2.h"
#include "e2_config_dialog.h"
#include "e2_dialog.h"
#include "e2_filetype.h"
#include "e2_option_tree.h"

static E2_OptionSet *set;
static E2_SpecificConfDialogRuntime *config;
static GtkWidget *editor;
static gchar *application_icon, *theme_icon;
static guint step;
static const gchar *expected[] = {
    "application", "application", "application", "application", "theme",
    "gtk-cut", "", "", "", "", "", "application", ""
};

static GtkWidget *find_label (GtkWidget *widget, const gchar *label)
{
    if (GTK_IS_LABEL (widget) && !strcmp (gtk_label_get_text (GTK_LABEL (widget)), label))
        return widget;
    if (!GTK_IS_CONTAINER (widget)) return NULL;
    GList *children = gtk_container_get_children (GTK_CONTAINER (widget));
    GtkWidget *found = NULL;
    for (GList *p = children; p != NULL && found == NULL; p = p->next)
        found = find_label (p->data, label);
    g_list_free (children);
    return found;
}

static void auto_icons (GtkWidget *dialog)
{
    GtkWidget *label = find_label (dialog, "Auto icons");
    g_assert_nonnull (label);
    GtkWidget *button = gtk_widget_get_ancestor (label, GTK_TYPE_BUTTON);
    g_assert_nonnull (button);
    gtk_button_clicked (GTK_BUTTON (button));
}

static GtkWidget *editor_window (void)
{
    GList *windows = gtk_window_list_toplevels ();
    GtkWidget *found = NULL;
    for (GList *p = windows; p != NULL; p = p->next)
    {
        const gchar *title = gtk_window_get_title (p->data);
        if (title != NULL && strstr (title, "edit filetypes") != NULL) { found = p->data; break; }
    }
    g_list_free (windows);
    g_assert_nonnull (found);
    return found;
}

static GtkTreeModel *command_model (GtkWidget *widget)
{
    if (GTK_IS_TREE_VIEW (widget))
    {
        GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));
        if (gtk_tree_model_get_n_columns (model) == 3) return model;
    }
    if (!GTK_IS_CONTAINER (widget)) return NULL;
    GList *children = gtk_container_get_children (GTK_CONTAINER (widget));
    GtkTreeModel *found = NULL;
    for (GList *p = children; p != NULL && found == NULL; p = p->next)
        found = command_model (p->data);
    g_list_free (children);
    return found;
}

static void check_icon (GtkTreeModel *model, const gchar *path, gint column, const gchar *expected_icon)
{
    GtkTreeIter iter;
    gchar *icon;
    g_assert_true (gtk_tree_model_get_iter_from_string (model, &iter, path));
    gtk_tree_model_get (model, &iter, column, &icon, -1);
    g_assert_cmpstr (icon, ==, expected_icon);
    g_free (icon);
}

static void check_assignments (GtkTreeModel *model, gboolean categories)
{
    for (guint i = 0; i < G_N_ELEMENTS (expected); i++)
    {
        gchar *path = g_strdup_printf (categories ? "0:1:%u" : "%u", i);
        const gchar *icon = !strcmp (expected[i], "application") ? application_icon :
            (!strcmp (expected[i], "theme") ? theme_icon : expected[i]);
        check_icon (model, path, categories ? 3 : 2, icon);
        g_free (path);
    }
}

static void check_runtime (void)
{
    const gchar **actions = e2_filetype_get_actions ("autoprobe");
    g_assert_nonnull (actions);
    for (guint i = 0; i < G_N_ELEMENTS (expected); i++)
    {
        const gchar *icon = !strcmp (expected[i], "application") ? application_icon :
            (!strcmp (expected[i], "theme") ? theme_icon : (*expected[i] ? expected[i] : NULL));
        g_assert_cmpstr (e2_filetype_get_action_icon (actions[i]), ==, icon);
    }
    g_assert_null (actions[G_N_ELEMENTS (expected)]);
}

static void setup (void)
{
    const gchar *root = g_getenv ("E2_AUTO_ICON_TEST");
    set = e2_option_get_simple ("filetypes");
    application_icon = g_build_filename (root, "application.png", NULL);
    theme_icon = g_build_filename (root, "data", "icons", "hicolor", "16x16", "apps", "e2-theme-probe.png", NULL);
    gchar *search = g_build_filename (root, "data", "icons", NULL);
    gtk_icon_theme_prepend_search_path (gtk_icon_theme_get_default (), search);
    g_free (search);
    g_object_set (gtk_settings_get_default (), "gtk-icon-theme-name", "Adwaita",
        "gtk-fallback-icon-theme", "hicolor", NULL);
    if (g_getenv ("E2_AUTO_ICON_RELOAD") != NULL) return;
    gchar *fixture = g_strdup_printf (
        "filetypes=<\nAutomatic icons||\n\t|extensions|\n\t\t|autoprobe|\n\t|commands|\n"
        "\t\t|Misleading label|e2-icon-probe %%f|\n"
        "\t\t|Quoted path| >! \"%s/bin with spaces/e2-icon-probe\" %%f|\n"
        "\t\t|Environment|env LANG=C e2-icon-probe --flag %%p|\n"
        "\t\t|Terminal|x e2-icon-probe %%f|\n"
        "\t\t|e2-theme-probe %%f||\n"
        "\t\t|Manual override|e2-theme-probe %%f|gtk-cut\n"
        "\t\t|Unmatched|e2-icon-not-installed %%f|\n"
        "\t\t|Variable|$EDITOR %%f|\n"
        "\t\t|Compound|e2-icon-probe;echo %%p|\n"
        "\t\t|Internal|file.view|\n"
        "\t\t|Malformed|\"unterminated|\n"
        "\t\t|Assignment|LANG=C e2-icon-probe %%f|\n"
        "\t\t|Shared launcher|e2-ambiguous-probe %%f|\n"
        "Other category||\n\t|extensions|\n\t\t|otherauto|\n\t|commands|\n"
        "\t\t|Other|e2-icon-probe %%f|\n>\n", root);
    gchar **lines = g_strsplit (fixture, "\n", -1);
    gtk_tree_store_clear (GTK_TREE_STORE (set->ex.tree.model));
    g_assert_true (e2_option_tree_set_from_array ("filetypes", lines, NULL, NULL));
    g_strfreev (lines);
    g_free (fixture);
    e2_filetype_apply_allnew ();
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
            setup ();
            if (g_getenv ("E2_AUTO_ICON_RELOAD") != NULL)
            {
                check_assignments (set->ex.tree.model, TRUE);
                check_runtime ();
                check_icon (set->ex.tree.model, "1:1:0", 3, application_icon);
                g_print ("automatic filetype icons reload passed\n");
                e2_main_closedown (TRUE, TRUE, TRUE);
                return FALSE;
            }
            e2_filetype_config_show ("autoprobe");
            editor = editor_window ();
            break;
        case 1:
            auto_icons (editor);
            check_assignments (command_model (editor), FALSE);
            /* A second click preserves all existing choices and remains harmless. */
            auto_icons (editor);
            check_assignments (command_model (editor), FALSE);
            check_icon (set->ex.tree.model, "0:1:0", 3, "");
            check_icon (set->ex.tree.model, "1:1:0", 3, "");
            g_assert_null (e2_filetype_get_action_icon (e2_filetype_get_actions ("autoprobe")[0]));
            gtk_dialog_response (GTK_DIALOG (editor), GTK_RESPONSE_CANCEL);
            check_icon (set->ex.tree.model, "0:1:0", 3, "");
            config = e2_config_dialog_single ("filetypes", e2_filetype_apply_allnew, TRUE);
            break;
        case 2:
            auto_icons (config->dialog);
            check_assignments (set->ex.tree.model, TRUE);
            check_icon (set->ex.tree.model, "1:1:0", 3, application_icon);
            /* Extension rows and headings are not commands. */
            check_icon (set->ex.tree.model, "0:0:0", 3, "");
            check_icon (set->ex.tree.model, "0:1", 3, "");
            g_assert_true ((set->ex.tree.flags & E2_OPTION_TREE_SET_EDITED) != 0);
            gtk_dialog_response (GTK_DIALOG (config->dialog), GTK_RESPONSE_CANCEL);
            check_icon (set->ex.tree.model, "0:1:0", 3, "");
            check_icon (set->ex.tree.model, "1:1:0", 3, "");
            config = e2_config_dialog_single ("filetypes", e2_filetype_apply_allnew, TRUE);
            break;
        case 3:
            auto_icons (config->dialog);
            gtk_dialog_response (GTK_DIALOG (config->dialog), E2_RESPONSE_APPLY);
            check_assignments (set->ex.tree.model, TRUE);
            check_runtime ();
            /* Removing a saved assignment must not trigger automatic refilling. */
            GtkTreeIter iter;
            g_assert_true (gtk_tree_model_get_iter_from_string (set->ex.tree.model, &iter, "0:1:0"));
            gtk_tree_store_set (GTK_TREE_STORE (set->ex.tree.model), &iter, 3, "", -1);
            e2_filetype_apply_allnew ();
            g_assert_null (e2_filetype_get_action_icon (e2_filetype_get_actions ("autoprobe")[0]));
            e2_filetype_config_show ("autoprobe");
            editor = editor_window ();
            break;
        case 4:
            check_icon (command_model (editor), "0", 2, "");
            auto_icons (editor);
            gtk_dialog_response (GTK_DIALOG (editor), E2_RESPONSE_APPLY);
            /* Restore the legacy unnamed row skipped by the existing editor. */
            GtkTreeIter commands, row;
            g_assert_true (gtk_tree_model_get_iter_from_string (set->ex.tree.model, &commands, "0:1"));
            gtk_tree_store_insert (GTK_TREE_STORE (set->ex.tree.model), &row, &commands, 4);
            gtk_tree_store_set (GTK_TREE_STORE (set->ex.tree.model), &row,
                0, "", 1, "e2-theme-probe %f", 2, "", 3, theme_icon, -1);
            e2_filetype_apply_allnew ();
            check_assignments (set->ex.tree.model, TRUE);
            check_runtime ();
            g_print ("automatic filetype icons buttons, matching, overrides and discard passed\n");
            e2_main_closedown (TRUE, TRUE, TRUE);
            return FALSE;
    }
    OPENBGL
    return TRUE;
}

__attribute__((constructor)) static void schedule (void)
{ unsetenv ("LD_PRELOAD"); g_timeout_add (500, tick, NULL); }
