/* Integration probe, preloaded only by run-modern-icons.sh. */
#include "emelfm2.h"
#include "e2_modern_ui.h"
#include "e2_icons.h"
#include "e2_option.h"
#include "e2_select_image_dialog.h"
#include "e2_dialog.h"
#include "e2_config_dialog.h"

static guint step;
static GtkWidget *dialog;
static E2_SID_Runtime *picker;
static gboolean modern;

static void equal_pixels (GdkPixbuf *actual, GdkPixbuf *expected)
{
    g_assert_nonnull (actual);
    g_assert_nonnull (expected);
    g_assert_cmpint (gdk_pixbuf_get_width (actual), ==, gdk_pixbuf_get_width (expected));
    g_assert_cmpint (gdk_pixbuf_get_height (actual), ==, gdk_pixbuf_get_height (expected));
    gint channels = gdk_pixbuf_get_n_channels (actual);
    g_assert_cmpint (channels, ==, gdk_pixbuf_get_n_channels (expected));
    for (gint y = 0; y < gdk_pixbuf_get_height (actual); y++)
        g_assert_cmpint (memcmp (gdk_pixbuf_get_pixels (actual) + y * gdk_pixbuf_get_rowstride (actual),
            gdk_pixbuf_get_pixels (expected) + y * gdk_pixbuf_get_rowstride (expected),
            channels * gdk_pixbuf_get_width (actual)), ==, 0);
}

static void check_inventory (const gchar *filename)
{
    gchar *path = g_build_filename (g_getenv ("E2_ICON_TEST_DIR"), filename, NULL), *contents;
    g_assert_true (g_file_get_contents (path, &contents, NULL, NULL));
    gchar **names = g_strsplit (contents, "\n", -1);
    for (guint i = 0; names[i] != NULL && *names[i] != '\0'; i++)
        for (gint size = 16; size <= 32; size += 8)
        {
            GdkPixbuf *icon = e2_modern_ui_icon (names[i], size, size);
            if (!modern) { g_assert_null (icon); continue; }
            if (icon == NULL) g_error ("Missing modern artwork: %s", names[i]);
            g_assert_cmpint (gdk_pixbuf_get_width (icon), ==, size);
            g_assert_cmpint (gdk_pixbuf_get_height (icon), ==, size);
            gboolean visible = FALSE;
            for (gint y = 0; y < size; y++)
                for (gint x = 0; x < size; x++)
                    visible |= gdk_pixbuf_get_pixels (icon)[y * gdk_pixbuf_get_rowstride (icon) + x * 4 + 3] != 0;
            g_assert_true (visible);
            if (size == 24 && !strcmp (filename, "stock.txt"))
            {
                GdkPixbuf *loaded = e2_icons_get_puxbuf (names[i], GTK_ICON_SIZE_LARGE_TOOLBAR, FALSE);
                if (loaded == NULL) g_error ("Stock cannot be loaded: %s", names[i]);
                equal_pixels (loaded, e2_modern_ui_icon (names[i],
                    gdk_pixbuf_get_width (loaded), gdk_pixbuf_get_height (loaded)));
            }
        }
    g_strfreev (names);
    g_free (contents);
    g_free (path);
}

static void open_picker (void)
{
    GdkEventButton event = { 0 };
    event.state = GDK_CONTROL_MASK; /* Reset the remembered directory. */
    g_object_set_data (G_OBJECT (app.main_window), "dialog-form", GINT_TO_POINTER (E2_CFGDLG_SINGLE));
    dialog = e2_sid_create (app.main_window, "Icon preview probe", "history", &event);
    g_assert_nonnull (dialog);
    picker = g_object_get_data (G_OBJECT (app.main_window), "Icon preview probe");
    g_assert_nonnull (picker);
}

static void check_model (GtkTreeModel *model, gboolean stock, gboolean bundled)
{
    GtkTreeIter iter;
    guint count = 0;
    gboolean valid = gtk_tree_model_get_iter_first (model, &iter);
    while (valid)
    {
        gchar *name, *path = NULL;
        GdkPixbuf *actual;
        gtk_tree_model_get (model, &iter, 0, &actual, 1, &name, -1);
        if (!stock) gtk_tree_model_get (model, &iter, 2, &path, -1);
        if (!bundled) g_assert_cmpstr (name, ==, "move");
        g_assert_nonnull (actual);
        gchar *id = stock ? g_strconcat ("gtk-", name, NULL) : g_strdup (name);
        gint w = gdk_pixbuf_get_width (actual), h = gdk_pixbuf_get_height (actual);
        GdkPixbuf *expected = e2_modern_ui_icon (id, w, h);
        if (modern && bundled && !g_str_has_prefix (name, "emelfm2"))
        {
            if (expected == NULL) g_error ("Missing picker preview: %s", id);
            equal_pixels (actual, expected);
        }
        else if (!stock)
        {
            expected = gdk_pixbuf_new_from_file_at_scale (path, w, h, TRUE, NULL);
            equal_pixels (actual, expected);
            g_object_unref (expected);
        }
        count++;
        g_object_unref (actual);
        g_free (name);
        g_free (id);
        g_free (path);
        valid = gtk_tree_model_iter_next (model, &iter);
    }
    g_assert_cmpuint (count, >, stock ? 90 : bundled ? 40 : 0);
}

static void select_icon (gboolean stock, const gchar *name, const gchar *saved)
{
    GtkTreeModel *model = stock ? picker->stockmodel : picker->custommodel;
    GtkIconView *view = stock ? picker->stockview : picker->customview;
    GtkTreeIter iter;
    gboolean found = FALSE, valid = gtk_tree_model_get_iter_first (model, &iter);
    while (valid)
    {
        gchar *label;
        gtk_tree_model_get (model, &iter, 1, &label, -1);
        found = !strcmp (label, name);
        g_free (label);
        if (found) break;
        valid = gtk_tree_model_iter_next (model, &iter);
    }
    if (!found) g_error ("Icon not selectable: %s", name);
    gtk_notebook_set_current_page (picker->notebook, stock ? 1 : 0);
    GtkTreePath *path = gtk_tree_model_get_path (model, &iter);
    gtk_icon_view_select_path (view, path);
    gtk_tree_path_free (path);
    gtk_dialog_response (GTK_DIALOG (dialog), E2_RESPONSE_MORE);
    g_assert_cmpstr (g_object_get_data (G_OBJECT (dialog), "image"), ==, saved);
    g_assert_nonnull (e2_icons_get_puxbuf (saved, GTK_ICON_SIZE_LARGE_TOOLBAR, FALSE));
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
            modern = e2_modern_ui_enabled ();
            check_inventory ("stock.txt");
            check_inventory ("bundled.txt");
            open_picker ();
            break;
        case 1:
            check_model (picker->stockmodel, TRUE, TRUE);
            check_model (picker->custommodel, FALSE, TRUE);
            select_icon (TRUE, "cut", "gtk-cut");
            select_icon (FALSE, "history", "history");
            select_icon (FALSE, "history_48", "history_48");
            /* An external file named like a built-in action keeps its pixels. */
            g_assert_true (gtk_file_chooser_set_current_folder (
                GTK_FILE_CHOOSER (picker->dir_chooser), g_getenv ("E2_ICON_CUSTOM_DIR")));
            break;
        case 2:
        {
            gchar *folder = gtk_file_chooser_get_current_folder (GTK_FILE_CHOOSER (picker->dir_chooser));
            g_assert_cmpstr (folder, ==, g_getenv ("E2_ICON_CUSTOM_DIR"));
            g_free (folder);
            check_model (picker->custommodel, FALSE, FALSE);
            gtk_widget_destroy (dialog);
            e2_option_set_from_string ("icon-dir", (gchar *)g_getenv ("E2_ICON_CUSTOM_DIR"));
            e2_option_bool_set ("use-icon-dir", TRUE);
            open_picker ();
            break;
        }
        case 3:
            g_assert_null (e2_modern_ui_icon ("gtk-cut", 24, 24));
            check_model (picker->custommodel, FALSE, FALSE);
            gtk_widget_destroy (dialog);
            g_print ("modern icon inventory, previews, History selection and custom files passed\n");
            e2_main_closedown (TRUE, TRUE, TRUE);
            return FALSE;
    }
    OPENBGL
    return TRUE;
}

__attribute__((constructor)) static void schedule (void)
{ unsetenv ("LD_PRELOAD"); g_timeout_add (500, tick, NULL); }
