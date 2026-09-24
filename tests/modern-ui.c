/* Integration probe, preloaded only by run-modern-ui.sh. */
#include "emelfm2.h"
#include "e2_modern_ui.h"
#include "e2_option.h"
#include "e2_window.h"
#include "e2_icons.h"
#include "e2_menu.h"
#include <string.h>

static guint step, styled;
static gboolean initially_enabled;
static GString *snapshot;
static GtkWidget *probe, *button, *entry;
static GtkWidget *menu, *menu_items[2];

static void menu_position (GtkMenu *popup, gint *x, gint *y,
    gboolean *push_in, gpointer data)
{ *x = 100; *y = 100; *push_in = FALSE; }

/* Check rendered pixels, not just style properties: GTK 2 theme engines can
 * draw a highlight from artwork even when NORMAL and PRELIGHT colors match. */
static void check_menu_highlight (guint selected)
{
    g_assert_cmpint (gtk_widget_get_state (menu_items[selected]), ==, GTK_STATE_PRELIGHT);
    g_assert_cmpint (gtk_widget_get_state (menu_items[1 - selected]), ==, GTK_STATE_NORMAL);
    GtkAllocation a;
    gtk_widget_get_allocation (menu, &a);
#ifdef USE_GTK3_0
    GdkPixbuf *pixels = gdk_pixbuf_get_from_window (gtk_widget_get_window (menu),
        0, 0, a.width, a.height);
#else
    GdkPixbuf *pixels = gdk_pixbuf_get_from_drawable (NULL, gtk_widget_get_window (menu),
        NULL, 0, 0, 0, 0, a.width, a.height);
#endif
    g_assert_nonnull (pixels);
    guchar *samples[2];
    for (guint i = 0; i < 2; i++)
    {
        gint x, y;
        gtk_widget_get_allocation (menu_items[i], &a);
        g_assert_true (gtk_widget_translate_coordinates (menu_items[i], menu,
            a.width - 25, a.height / 2, &x, &y));
        samples[i] = gdk_pixbuf_get_pixels (pixels) + y * gdk_pixbuf_get_rowstride (pixels)
            + x * gdk_pixbuf_get_n_channels (pixels);
    }
    gint difference = 0;
    for (guint channel = 0; channel < 3; channel++)
        difference = MAX (difference, ABS ((gint)samples[0][channel] - samples[1][channel]));
    const gchar *directory = g_getenv ("E2_MODERN_CAPTURE_DIR");
    if (directory != NULL)
    {
        gchar *path = g_strdup_printf ("%s/%s-menu-%u.png", directory,
            g_getenv ("E2_MODERN_TEST_CASE"), selected);
        g_assert_true (gdk_pixbuf_save (pixels, path, "png", NULL, NULL));
        g_free (path);
    }
    g_object_unref (pixels);
    g_assert_cmpint (difference, >=, 45);
}

static void record (GtkWidget *widget, gpointer unused)
{
    GtkAllocation a;
    gtk_widget_get_allocation (widget, &a);
    if (gtk_widget_get_visible (widget))
    {
        g_string_append_printf (snapshot, "%s %d %d %d %d", G_OBJECT_TYPE_NAME (widget),
            a.x, a.y, a.width, a.height);
#ifdef USE_GTK3_0
        GtkStyleContext *context = gtk_widget_get_style_context (widget);
        GdkRGBA fg, bg;
        gtk_style_context_get_color (context, GTK_STATE_FLAG_NORMAL, &fg);
        gtk_style_context_get_background_color (context, GTK_STATE_FLAG_NORMAL, &bg);
        g_string_append_printf (snapshot, " fg=%.4f,%.4f,%.4f,%.4f bg=%.4f,%.4f,%.4f,%.4f",
            fg.red, fg.green, fg.blue, fg.alpha, bg.red, bg.green, bg.blue, bg.alpha);
#else
        GtkStyle *style = gtk_widget_get_style (widget);
        if (!strcmp (G_OBJECT_TYPE_NAME (style), "E2FlatStyle")) styled++;
        g_string_append_printf (snapshot, " thickness=%d,%d", style->xthickness, style->ythickness);
        if (GTK_IS_BUTTON (widget)) {
            GtkBorder *border=NULL;
            gtk_widget_style_get (widget, "inner-border", &border, NULL);
            if (border != NULL) { g_string_append_printf (snapshot, " inner=%d,%d,%d,%d",border->left,border->right,border->top,border->bottom);gtk_border_free(border); }
        }
        for (guint i = 0; i < 5; i++)
        {
            GdkColor c[] = { style->fg[i], style->bg[i], style->base[i], style->text[i] };
            for (guint j = 0; j < G_N_ELEMENTS (c); j++)
                g_string_append_printf (snapshot, " %04x%04x%04x", c[j].red, c[j].green, c[j].blue);
        }
#endif
        g_string_append_c (snapshot, '\n');
    }
    if (GTK_IS_CONTAINER (widget)) gtk_container_forall (GTK_CONTAINER (widget), record, NULL);
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
#ifdef USE_GTK3_0
            /* Compare settled theme colors, not a frame of a CSS transition. */
            g_object_set (gtk_settings_get_default (), "gtk-enable-animations", FALSE, NULL);
#endif
            initially_enabled = e2_modern_ui_enabled ();
#ifdef E2_MODERN_UI
            g_assert_nonnull (e2_option_get ("modern-ui"));
            g_assert_cmpint (initially_enabled, ==, e2_option_bool_get ("modern-ui"));
#else
            g_assert_null (e2_option_get ("modern-ui"));
            g_assert_false (initially_enabled);
#endif
            probe = gtk_window_new (GTK_WINDOW_TOPLEVEL);
            GtkWidget *box = gtk_vbox_new (FALSE, 0);
            gtk_container_add (GTK_CONTAINER (probe), box);
            button = gtk_button_new_with_label ("Appearance probe");
            entry = gtk_entry_new ();
            gtk_box_pack_start (GTK_BOX (box), button, FALSE, FALSE, 0);
            gtk_box_pack_start (GTK_BOX (box), entry, FALSE, FALSE, 0);
            gtk_widget_show_all (probe);
            /* Use the same item factory as bookmarks and context menus. */
            menu = e2_menu_get ();
            menu_items[0] = e2_menu_add (menu, "Home                         ", NULL, NULL, NULL, NULL);
            menu_items[1] = e2_menu_add (menu, "Documents                    ", NULL, NULL, NULL, NULL);
            gtk_menu_popup (GTK_MENU (menu), NULL, NULL, menu_position, NULL, 0, GDK_CURRENT_TIME);
            gint x, y, origin_x, origin_y;
            gtk_widget_translate_coordinates (menu_items[0], menu, 50, 10, &x, &y);
            gdk_window_get_origin (gtk_widget_get_window (menu), &origin_x, &origin_y);
            gdk_display_warp_pointer (gtk_widget_get_display (menu), gtk_widget_get_screen (menu),
                origin_x + x, origin_y + y);
            break;
        }
        case 1:
            check_menu_highlight (0); /* Pointer hover. */
            /* The action signal bound to Down in a popup menu. */
            g_signal_emit_by_name (menu, "move-current", GTK_MENU_DIR_NEXT);
            break;
        case 2:
        {
            check_menu_highlight (1); /* Keyboard selection. */
            gtk_menu_popdown (GTK_MENU (menu));
            gtk_widget_destroy (menu);
            snapshot = g_string_new (NULL);
            record (app.main_window, NULL);
            record (probe, NULL);
            gchar *path = g_build_filename (g_getenv ("E2_MODERN_TEST_DIR"),
                g_getenv ("E2_MODERN_TEST_CASE"), NULL);
            g_assert_true (g_file_set_contents (path, snapshot->str, snapshot->len, NULL));
            g_free (path);
            g_string_free (snapshot, TRUE);
#ifndef USE_GTK3_0
            if (initially_enabled) g_assert_cmpuint (styled, >, 20);
            else g_assert_cmpuint (styled, ==, 0);
            /* App-owned color changes must still work after attaching flat styles. */
            GdkColor custom;
            gdk_color_parse ("#445566", &custom);
            gtk_widget_modify_bg (button, GTK_STATE_NORMAL, &custom);
            GtkStyle *style = gtk_widget_get_style (button);
            g_assert_cmpint (style->bg[GTK_STATE_NORMAL].red, ==, custom.red);
            g_assert_cmpint (style->bg[GTK_STATE_NORMAL].green, ==, custom.green);
            g_assert_cmpint (style->bg[GTK_STATE_NORMAL].blue, ==, custom.blue);
#endif
#ifdef E2_MODERN_UI
            GdkPixbuf *icon = e2_modern_ui_icon ("gtk-copy", 19, 15);
            if (initially_enabled)
            {
                g_assert_nonnull (icon);
                g_assert_cmpint (gdk_pixbuf_get_width (icon), ==, 19);
                g_assert_cmpint (gdk_pixbuf_get_height (icon), ==, 15);
                e2_option_bool_set ("use-icon-dir", TRUE);
                g_assert_null (e2_modern_ui_icon ("gtk-copy", 19, 15));
                e2_option_bool_set ("use-icon-dir", FALSE);
                g_assert_null (e2_modern_ui_icon ("/tmp/custom.png", 19, 15));
                g_assert_null (e2_modern_ui_icon ("unmapped-user-icon", 19, 15));
            }
            else g_assert_null (icon);
            e2_option_bool_set ("modern-ui", !initially_enabled);
            g_assert_cmpint (e2_modern_ui_enabled (), ==, initially_enabled);
#endif
            e2_window_recreate (&app.window);
            break;
        }
        case 3:
            g_assert_cmpint (e2_modern_ui_enabled (), ==, initially_enabled);
            gtk_widget_destroy (probe);
            e2_main_closedown (TRUE, TRUE, TRUE);
            return FALSE;
    }
    OPENBGL
    return TRUE;
}
__attribute__((constructor)) static void schedule (void)
{ unsetenv ("LD_PRELOAD"); g_timeout_add (500, tick, NULL); }
