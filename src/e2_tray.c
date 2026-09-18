/* Tray integration. Licensed under GPL version 3 or later. */
#include "emelfm2.h"
#include "e2_tray.h"
#include "e2_option.h"
#include "e2_icons.h"
#include "e2_output.h"
#include <gmodule.h>

#ifdef USE_GTK2_10
/* Keep these values in step with the tray-behaviour option. */
enum { E2_TRAY_X11, E2_TRAY_XFCE, E2_TRAY_GNOME };
static GtkStatusIcon *status_icon;
static GObject *indicator;
static GtkWidget *tray_menu;
static gint tray_mode = -1;
static gboolean hidden_by_tray;
static guint attention_count;

/* Optional runtime dependency, always matching the application's GTK ABI.
   These are the stable AppIndicator C ABI values: ApplicationStatus = 0,
   Passive = 0, Active = 1. The module stays resident because it registers types. */
static GModule *indicator_module;
static GObject *(*indicator_new) (const gchar *, const gchar *, gint);
static void (*indicator_set_menu) (GObject *, GtkMenu *);
static void (*indicator_set_status) (GObject *, gint);
static void (*indicator_set_icon) (GObject *, const gchar *);
static void (*indicator_set_attention_icon) (GObject *, const gchar *);
static void (*indicator_set_label) (GObject *, const gchar *, const gchar *);

static void _e2_tray_show (void)
{
	hidden_by_tray = FALSE;
	gtk_widget_show (app.main_window);
	gtk_window_deiconify (GTK_WINDOW (app.main_window));
	e2_tray_windows_restore ();
	gtk_window_present (GTK_WINDOW (app.main_window));
}

static void _e2_tray_toggle_cb (gpointer object, gpointer data)
{
#ifdef USE_GTK2_18
	gboolean visible = gtk_widget_get_visible (app.main_window);
	GdkWindow *window = gtk_widget_get_window (app.main_window);
#else
	gboolean visible = GTK_WIDGET_VISIBLE (app.main_window);
	GdkWindow *window = app.main_window->window;
#endif
	NEEDCLOSEBGL
	if (visible && window != NULL
		&& !(gdk_window_get_state (window) & GDK_WINDOW_STATE_ICONIFIED))
	{
		hidden_by_tray = TRUE;
		e2_tray_windows_hide ();
		gtk_widget_hide (app.main_window);
	}
	else
		_e2_tray_show ();
	NEEDOPENBGL
}

static void _e2_tray_quit_cb (GtkMenuItem *item, gpointer data)
{
	NEEDCLOSEBGL
	/* Keep shutdown confirmation dialogs reachable when the window is hidden. */
	_e2_tray_show ();
	e2_main_closedown (FALSE, TRUE, TRUE);
	NEEDOPENBGL
}

static void _e2_tray_popup_cb (GtkStatusIcon *icon, guint button,
	guint activate_time, gpointer data)
{
	NEEDCLOSEBGL
	gtk_menu_popup (GTK_MENU (tray_menu), NULL, NULL,
		gtk_status_icon_position_menu, icon, button, activate_time);
	NEEDOPENBGL
}

static void _e2_tray_embedded_cb (GObject *object, GParamSpec *pspec,
	gpointer data)
{
	NEEDCLOSEBGL
	if (hidden_by_tray && !gtk_status_icon_is_embedded (status_icon))
		_e2_tray_show ();
	NEEDOPENBGL
}

static void _e2_tray_connected_cb (GObject *object, gboolean connected,
	gpointer data)
{
	NEEDCLOSEBGL
	if (!connected && hidden_by_tray)
		_e2_tray_show ();
	NEEDOPENBGL
}

static gboolean _e2_tray_load_indicator (void)
{
	if (indicator_module != NULL)
		return TRUE;
	const gchar *libraries[] = {
#ifdef USE_GTK3_0
		"libayatana-appindicator3.so.1", "libappindicator3.so.1",
#else
		"libayatana-appindicator.so.1", "libappindicator.so.1",
#endif
		NULL
	};
	guint i;
	for (i = 0; libraries[i] != NULL; i++)
	{
		GModule *module = g_module_open (libraries[i], G_MODULE_BIND_LAZY
			| G_MODULE_BIND_LOCAL);
		if (module == NULL)
			continue;
		if (g_module_symbol (module, "app_indicator_new", (gpointer*)&indicator_new)
			&& g_module_symbol (module, "app_indicator_set_menu", (gpointer*)&indicator_set_menu)
			&& g_module_symbol (module, "app_indicator_set_status", (gpointer*)&indicator_set_status)
			&& g_module_symbol (module, "app_indicator_set_icon", (gpointer*)&indicator_set_icon))
		{
			g_module_symbol (module, "app_indicator_set_attention_icon", (gpointer*)&indicator_set_attention_icon);
			g_module_symbol (module, "app_indicator_set_label", (gpointer*)&indicator_set_label);
			g_module_make_resident (module);
			indicator_module = module;
			return TRUE;
		}
		g_module_close (module);
	}
	return FALSE;
}

static void _e2_tray_update_icon (void)
{
	if (status_icon != NULL)
	{
		GList *icons = e2_icons_get_application ();
		if (icons == NULL)
		{
			GdkPixbuf *themed = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
				BINNAME, 32, 0, NULL);
			if (themed != NULL) icons = g_list_append (NULL, themed);
		}
		if (icons != NULL)
		{
			GdkPixbuf *pixbuf = g_list_last (icons)->data;
			if (attention_count != 0)
			{
				pixbuf = gdk_pixbuf_copy (pixbuf);
				gint size = MAX (1, MIN (gdk_pixbuf_get_width (pixbuf), gdk_pixbuf_get_height (pixbuf)) / 3);
				GdkPixbuf *badge = gdk_pixbuf_new_subpixbuf (pixbuf,
					gdk_pixbuf_get_width (pixbuf) - size, gdk_pixbuf_get_height (pixbuf) - size,
					size, size);
				gdk_pixbuf_fill (badge, 0xffa000ff);
				g_object_unref (badge);
			}
			gtk_status_icon_set_from_pixbuf (status_icon, pixbuf);
			if (attention_count != 0) g_object_unref (pixbuf);
			g_list_foreach (icons, (GFunc)g_object_unref, NULL);
			g_list_free (icons);
		}
		else
			gtk_status_icon_set_from_icon_name (status_icon, BINNAME);
	}
	if (indicator != NULL)
	{
		gchar *path = e2_icons_get_application_path ();
		indicator_set_icon (indicator, path != NULL ? path : BINNAME);
		if (indicator_set_attention_icon != NULL)
			indicator_set_attention_icon (indicator, path != NULL ? path : BINNAME);
		g_free (path);
	}
}
#endif

/* Called with the UI lock held, after options and window are available. */
void e2_tray_sync (void)
{
#ifdef USE_GTK2_10
	gint mode = e2_option_bool_get ("tray-enabled")
		? e2_option_sel_get ("tray-behaviour") : -1;
	if (mode == tray_mode)
	{
		_e2_tray_update_icon ();
		e2_tray_windows_sync (mode >= 0);
		return;
	}
	/* Restore a hidden window before removing its only way back. */
	if (hidden_by_tray)
		_e2_tray_show ();
	e2_tray_cleanup ();
	e2_tray_windows_sync (FALSE);
	if (mode < 0)
		return;
	if (mode != E2_TRAY_X11 && !_e2_tray_load_indicator ())
	{
		e2_output_print_error (_("Tray support for XFCE and GNOME requires the Ayatana AppIndicator or AppIndicator library matching this application's GTK version."), FALSE);
		return;
	}

	tray_mode = mode;
	tray_menu = gtk_menu_new ();
	g_object_ref_sink (tray_menu);
	GtkWidget *item = gtk_menu_item_new_with_mnemonic (_("_Show/hide window"));
	gtk_menu_shell_append (GTK_MENU_SHELL (tray_menu), item);
	g_signal_connect (item, "activate", G_CALLBACK (_e2_tray_toggle_cb), NULL);
	item = gtk_separator_menu_item_new ();
	gtk_menu_shell_append (GTK_MENU_SHELL (tray_menu), item);
	item = gtk_menu_item_new_with_mnemonic (_("_Quit"));
	gtk_menu_shell_append (GTK_MENU_SHELL (tray_menu), item);
	g_signal_connect (item, "activate", G_CALLBACK (_e2_tray_quit_cb), NULL);
	gtk_widget_show_all (tray_menu);
	e2_tray_windows_menu (tray_menu);

	if (mode == E2_TRAY_X11)
	{
		status_icon = gtk_status_icon_new ();
		g_signal_connect (status_icon, "activate", G_CALLBACK (_e2_tray_toggle_cb), NULL);
		g_signal_connect (status_icon, "popup-menu", G_CALLBACK (_e2_tray_popup_cb), NULL);
		g_signal_connect (status_icon, "notify::embedded", G_CALLBACK (_e2_tray_embedded_cb), NULL);
		_e2_tray_update_icon ();
		gtk_status_icon_set_visible (status_icon, TRUE);
	}
	else
	{
		/* The desktop owns click routing: primary click displays our exported
		   menu; secondary click is left to the panel/indicator host. */
		indicator = indicator_new (BINNAME, BINNAME, 0);
		g_signal_connect (indicator, "connection-changed",
			G_CALLBACK (_e2_tray_connected_cb), NULL);
		indicator_set_menu (indicator, GTK_MENU (tray_menu));
		_e2_tray_update_icon ();
		indicator_set_status (indicator, 1);
	}
	e2_tray_windows_sync (TRUE);
#endif
}

void e2_tray_cleanup (void)
{
#ifdef USE_GTK2_10
	if (status_icon != NULL)
	{
		g_signal_handlers_disconnect_by_func (status_icon,
			G_CALLBACK (_e2_tray_embedded_cb), NULL);
		gtk_status_icon_set_visible (status_icon, FALSE);
		g_object_unref (status_icon);
		status_icon = NULL;
	}
	if (indicator != NULL)
	{
		g_signal_handlers_disconnect_by_func (indicator,
			G_CALLBACK (_e2_tray_connected_cb), NULL);
		indicator_set_status (indicator, 0);
		g_object_unref (indicator);
		indicator = NULL;
	}
	if (tray_menu != NULL)
	{
		gtk_widget_destroy (tray_menu);
		g_object_unref (tray_menu);
		tray_menu = NULL;
	}
	tray_mode = -1;
	hidden_by_tray = FALSE;
#endif
}

/* Entry points shared with window and notification management. */
gboolean e2_tray_is_active (void)
{
#ifdef USE_GTK2_10
	return tray_mode >= 0;
#else
	return FALSE;
#endif
}

gboolean e2_tray_is_hidden (void)
{
#ifdef USE_GTK2_10
	return hidden_by_tray;
#else
	return FALSE;
#endif
}

void e2_tray_show_main (void)
{
#ifdef USE_GTK2_10
	_e2_tray_show ();
#else
	gtk_window_present (GTK_WINDOW (app.main_window));
#endif
}

void e2_tray_set_attention (guint count)
{
#ifdef USE_GTK2_10
	attention_count = count;
	_e2_tray_update_icon ();
	if (status_icon != NULL)
	{
		gchar *tip = count ? g_strdup_printf (_("%s: needs attention (%u)"), PROGNAME, count)
			: g_strdup (PROGNAME);
#ifdef USE_GTK2_16
		gtk_status_icon_set_tooltip_text (status_icon, tip);
#else
		gtk_status_icon_set_tooltip (status_icon, tip);
#endif
		g_free (tip);
	}
	if (indicator != NULL)
	{
		indicator_set_status (indicator, count ? 2 : 1);
		if (indicator_set_label != NULL)
		{
			gchar *label = count ? g_strdup_printf ("! %u", count) : g_strdup ("");
			indicator_set_label (indicator, label, "! 99");
			g_free (label);
		}
	}
#endif
}

void e2_tray_show_for_review (void)
{
#ifdef USE_GTK2_10
	hidden_by_tray = FALSE;
#endif
	gtk_widget_show (app.main_window);
	gtk_window_deiconify (GTK_WINDOW (app.main_window));
	gtk_window_present (GTK_WINDOW (app.main_window));
}
