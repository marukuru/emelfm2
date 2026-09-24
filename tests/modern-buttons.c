/* Exercise real toolbar/dialog button painting, preloaded by run-modern-buttons.sh. */
#include "emelfm2.h"
#include "e2_modern_ui.h"
#include "e2_toolbar.h"
#include "e2_button.h"

static GtkWidget *probe, *plain, *flat, *toggle, *neutral, *command, *dialog_button;
static guint step, clicks;
static gboolean modern;

static GtkWidget *find_button (GtkWidget *widget)
{
    if (GTK_IS_BUTTON (widget) && gtk_widget_get_mapped (widget)
        && gtk_widget_is_sensitive (widget)) return widget;
    if (!GTK_IS_CONTAINER (widget)) return NULL;
    GList *children = gtk_container_get_children (GTK_CONTAINER (widget));
    GtkWidget *found = NULL;
    for (GList *item = children; item != NULL && found == NULL; item = item->next)
        found = find_button (item->data);
    g_list_free (children);
    return found;
}

static void pointer_to (GtkWidget *widget)
{
    gint x, y, origin_x, origin_y;
    GtkAllocation a;
    gtk_widget_get_allocation (widget, &a);
    GtkWidget *top = gtk_widget_get_toplevel (widget);
    g_assert_true (gtk_widget_translate_coordinates (widget, top, a.width / 2, a.height / 2, &x, &y));
    gdk_window_get_origin (gtk_widget_get_window (top), &origin_x, &origin_y);
    gdk_display_warp_pointer (gtk_widget_get_display (widget), gtk_widget_get_screen (widget),
        origin_x + x, origin_y + y);
}

static void check (GtkWidget *widget, gboolean highlighted, const gchar *name)
{
    GtkAllocation a;
    gtk_widget_get_allocation (widget, &a);
    GtkWidget *top = gtk_widget_get_toplevel (widget);
    gint x, y;
    g_assert_true (gtk_widget_translate_coordinates (widget, top, 0, 0, &x, &y));
#ifdef USE_GTK3_0
    GdkPixbuf *pixels = gdk_pixbuf_get_from_window (gtk_widget_get_window (top), x, y, a.width, a.height);
    GdkRGBA color;
    g_assert_true (gtk_style_context_lookup_color (gtk_widget_get_style_context (widget),
        "theme_selected_bg_color", &color));
    gint accent[] = { color.red * 255 + .5, color.green * 255 + .5, color.blue * 255 + .5 };
#else
    GdkPixbuf *pixels = gdk_pixbuf_get_from_drawable (NULL, gtk_widget_get_window (top), NULL,
        x, y, 0, 0, a.width, a.height);
    GdkColor color = gtk_widget_get_style (widget)->bg[GTK_STATE_SELECTED];
    gint accent[] = { color.red / 257, color.green / 257, color.blue / 257 };
#endif
    g_assert_nonnull (pixels);
    const gchar *directory = g_getenv ("E2_BUTTON_CAPTURE_DIR");
    if (directory != NULL)
    {
        gchar *path = g_strdup_printf ("%s/%s-%s.png", directory, g_getenv ("E2_BUTTON_TEST_CASE"), name);
        g_assert_true (gdk_pixbuf_save (pixels, path, "png", NULL, NULL));
        g_free (path);
    }
    if (modern)
    {
        guint border = 0, interior = 0;
        for (gint py = 0; py < a.height; py++)
            for (gint px = 0; px < a.width; px++)
            {
                guchar *p = gdk_pixbuf_get_pixels (pixels) + py * gdk_pixbuf_get_rowstride (pixels)
                    + px * gdk_pixbuf_get_n_channels (pixels);
                gboolean matches = ABS (p[0] - accent[0]) < 10 && ABS (p[1] - accent[1]) < 10
                    && ABS (p[2] - accent[2]) < 10;
                if (px < 4 || px >= a.width - 4 || py < 4 || py >= a.height - 4)
                    border += matches;
                else if (px >= 10 && px < a.width - 10 && py >= 10 && py < a.height - 10)
                    interior += matches;
            }
        if (highlighted && border < 8) g_error ("%s: accent border missing (%u pixels)", name, border);
        if (!highlighted && border != 0) g_error ("%s: unexpected accent border (%u pixels)", name, border);
        /* Existing pressed/checked fills belong to the theme. Hover/focus must
         * not add an accent fill to ordinary command or dialog buttons. */
        if (!GTK_IS_TOGGLE_BUTTON (widget) && gtk_widget_get_state (widget) != GTK_STATE_ACTIVE)
            g_assert_cmpuint (interior, ==, 0);
        if (highlighted && gtk_widget_has_focus (widget)
            && !GTK_IS_TOGGLE_BUTTON (widget) && gtk_widget_get_state (widget) != GTK_STATE_ACTIVE)
        {
            /* Scan horizontal strokes above the caption. Counting separated
             * bands catches an extra inner focus rectangle, not antialiasing. */
            guint strokes = 0;
            gboolean previous = FALSE;
            for (gint py = 0; py < a.height / 2; py++)
            {
                guint matches = 0;
                for (gint px = 6; px < a.width - 6; px++)
                {
                    guchar *p = gdk_pixbuf_get_pixels (pixels) + py * gdk_pixbuf_get_rowstride (pixels)
                        + px * gdk_pixbuf_get_n_channels (pixels);
                    matches += ABS (p[0] - accent[0]) < 10 && ABS (p[1] - accent[1]) < 10
                        && ABS (p[2] - accent[2]) < 10;
                }
                gboolean stroke = matches > (a.width - 12) / 4;
                if (stroke && !previous) strokes++;
                previous = stroke;
            }
            if (strokes != 1) g_error ("%s: expected one highlighted outline, found %u", name, strokes);
        }
    }
    g_object_unref (pixels);
}

static void clicked (GtkWidget *widget, gpointer unused) { clicks++; }

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
            modern = e2_modern_ui_enabled ();
            command = find_button (GTK_WIDGET (app.commandbar.toolbar));
            g_assert_nonnull (command);
            probe = gtk_dialog_new ();
            gtk_window_move (GTK_WINDOW (probe), 50, 50);
            gtk_window_set_default_size (GTK_WINDOW (probe), 340, 200);
            GtkWidget *box = gtk_vbox_new (FALSE, 12);
            gtk_container_set_border_width (GTK_CONTAINER (box), 12);
            gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (probe))), box, TRUE, TRUE, 0);
            plain = e2_button_get_full ("Dialog button", NULL, GTK_ICON_SIZE_BUTTON, NULL,
                clicked, NULL, E2_BUTTON_CAN_FOCUS);
            flat = e2_button_get_full ("Flat button", NULL, GTK_ICON_SIZE_BUTTON, NULL,
                NULL, NULL, E2_BUTTON_CAN_FOCUS);
            gtk_button_set_relief (GTK_BUTTON (flat), GTK_RELIEF_NONE);
            toggle = gtk_toggle_button_new_with_label ("Toggle button");
            neutral = gtk_entry_new ();
            dialog_button = e2_button_get ("Delete", GTK_STOCK_DELETE, NULL, NULL, NULL);
            gtk_dialog_add_action_widget (GTK_DIALOG (probe), dialog_button, GTK_RESPONSE_OK);
            gtk_dialog_set_default_response (GTK_DIALOG (probe), GTK_RESPONSE_OK);
            gtk_box_pack_start (GTK_BOX (box), neutral, FALSE, FALSE, 0);
            gtk_box_pack_start (GTK_BOX (box), plain, FALSE, FALSE, 0);
            gtk_box_pack_start (GTK_BOX (box), flat, FALSE, FALSE, 0);
            gtk_box_pack_start (GTK_BOX (box), toggle, FALSE, FALSE, 0);
            gtk_widget_show_all (probe);
            gtk_widget_grab_focus (neutral);
            gtk_window_present (GTK_WINDOW (probe));
            gdk_window_focus (gtk_widget_get_window (probe), GDK_CURRENT_TIME);
            pointer_to (neutral);
            break;
        }
        case 1:
            check (plain, FALSE, "normal");
            check (command, FALSE, "toolbar-normal");
            pointer_to (command);
            break;
        case 2:
            g_assert_cmpint (gtk_widget_get_state (command), ==, GTK_STATE_PRELIGHT);
            check (command, TRUE, "toolbar-hover");
            pointer_to (plain);
            break;
        case 3:
            g_assert_cmpint (gtk_widget_get_state (plain), ==, GTK_STATE_PRELIGHT);
            check (plain, TRUE, "hover");
            gtk_button_pressed (GTK_BUTTON (plain));
            break;
        case 4:
            g_assert_cmpint (gtk_widget_get_state (plain), ==, GTK_STATE_ACTIVE);
            check (plain, TRUE, "pressed");
            gtk_button_released (GTK_BUTTON (plain));
            break;
        case 5:
            g_assert_cmpuint (clicks, ==, 1);
            pointer_to (neutral);
#if GTK_CHECK_VERSION(3,2,0)
            /* Match keyboard navigation, which enables GTK 3's focus ring. */
            gtk_window_set_focus_visible (GTK_WINDOW (probe), TRUE);
#endif
            gtk_widget_grab_focus (plain);
            break;
        case 6:
            g_assert_true (gtk_widget_has_focus (plain));
            check (plain, TRUE, "focus");
            gtk_widget_grab_focus (neutral);
            pointer_to (flat);
            break;
        case 7:
            check (flat, TRUE, "flat-hover");
            pointer_to (neutral);
            gtk_widget_grab_focus (flat);
            break;
        case 8:
            g_assert_true (gtk_widget_has_focus (flat));
            check (flat, TRUE, "flat-focus");
            gtk_widget_grab_focus (neutral);
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), TRUE);
            break;
        case 9:
            check (toggle, TRUE, "checked");
            gtk_widget_set_sensitive (toggle, FALSE);
            pointer_to (toggle);
            break;
        case 10:
            check (toggle, FALSE, "disabled-checked");
            gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (toggle), FALSE);
            gtk_widget_set_sensitive (toggle, TRUE);
            pointer_to (neutral);
            break;
        case 11:
            check (toggle, FALSE, "unchecked");
            check (plain, FALSE, "normal-again");
            check (flat, FALSE, "flat-normal-again");
            check (command, FALSE, "toolbar-normal-again");
            gtk_widget_grab_focus (dialog_button);
            break;
        case 12:
            g_assert_true (gtk_widget_has_focus (dialog_button));
            g_assert_true (gtk_widget_has_default (dialog_button));
            check (dialog_button, TRUE, "dialog-default-focus");
            pointer_to (dialog_button);
            break;
        case 13:
            check (dialog_button, TRUE, "dialog-default-focus-hover");
            gtk_widget_grab_focus (flat);
            pointer_to (flat);
            break;
        case 14:
            check (flat, TRUE, "flat-focus-hover");
            gtk_widget_destroy (probe);
            g_print ("button hover, focus, press, toggle, disabled state and activation passed\n");
            e2_main_closedown (TRUE, TRUE, TRUE);
            return FALSE;
    }
    OPENBGL
    return TRUE;
}

__attribute__((constructor)) static void schedule (void)
{ unsetenv ("LD_PRELOAD"); g_timeout_add (500, tick, NULL); }
