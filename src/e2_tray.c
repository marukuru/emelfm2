/* Tray integration. Licensed under GPL version 3 or later. */
#include "emelfm2.h"
#include "e2_tray.h"
#include "e2_tray_indicator.h"
#include "e2_option.h"
#include "e2_icons.h"
#include "e2_output.h"
#include <gmodule.h>
#include <glib/gstdio.h>
#include <unistd.h>

#ifdef USE_GTK2_10
/* Keep these values in step with the tray-behaviour option. */
enum { E2_TRAY_X11, E2_TRAY_XFCE, E2_TRAY_GNOME };
static GtkStatusIcon *status_icon;
static GObject *indicator;
static GtkWidget *tray_menu;
static gint tray_mode = -1;
static gboolean hidden_by_tray;
static guint attention_count;
/* Three slow pulses of the badge only, then a steady dot. Never blink the
   application icon or keep a timer running for an unanswered question. */
static const guint pulse_frames[] = { 4, 3, 2, 1, 0, 1, 2, 3 };
static GdkPixbuf *tray_pixbuf, *attention_frames[5];
static gchar *tray_icon_path, *attention_paths[5];
static gboolean attention_active;
static guint attention_source, attention_tick;

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
static void (*indicator_stop) (GObject *);
static void (*indicator_update_menu) (GObject *, GtkMenu *);

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
	/* Shutdown can run a confirmation loop. Ignore another Quit activation
	   until that loop ends, and never restore windows just to close them. */
	static gboolean quitting;
	if (!quitting)
	{
		quitting = TRUE;
		e2_main_closedown (FALSE, TRUE, TRUE);
		quitting = FALSE;
	}
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
#if GLIB_CHECK_VERSION(2,26,0)
	/* The desktop protocol is independent of GTK. Prefer this transport so
	   a GTK 2 application works with a GTK 3 XFCE indicator plugin as well. */
	if (e2_tray_indicator_available ())
	{
		indicator_new = e2_tray_indicator_new;
		indicator_set_menu = indicator_update_menu = e2_tray_indicator_set_menu;
		indicator_set_status = e2_tray_indicator_set_status;
		indicator_set_icon = e2_tray_indicator_set_icon;
		indicator_set_attention_icon = e2_tray_indicator_set_attention_icon;
		indicator_set_label = e2_tray_indicator_set_label;
		indicator_stop = e2_tray_indicator_stop;
		return TRUE;
	}
#endif
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

static void _e2_tray_free_icons (void)
{
	guint i;
	for (i = 0; i < G_N_ELEMENTS (attention_frames); i++)
	{
		if (attention_frames[i] != NULL)
			g_object_unref (attention_frames[i]);
		attention_frames[i] = NULL;
		if (attention_paths[i] != NULL)
		{
			g_unlink (attention_paths[i]);
			g_free (attention_paths[i]);
		}
		attention_paths[i] = NULL;
	}
	if (tray_pixbuf != NULL)
		g_object_unref (tray_pixbuf);
	tray_pixbuf = NULL;
	g_free (tray_icon_path);
	tray_icon_path = NULL;
}

static void _e2_tray_load_icons (void)
{
	_e2_tray_free_icons ();
	GList *icons = e2_icons_get_application ();
	if (icons != NULL)
	{
		tray_pixbuf = g_object_ref (g_list_last (icons)->data);
		g_list_foreach (icons, (GFunc)g_object_unref, NULL);
		g_list_free (icons);
	}
	else
		tray_pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
			BINNAME, 32, 0, NULL);
	tray_icon_path = e2_icons_get_application_path ();
}

static void _e2_tray_prepare_attention (void)
{
	if (tray_pixbuf == NULL || attention_frames[0] != NULL)
		return;
	gint width = gdk_pixbuf_get_width (tray_pixbuf);
	gint height = gdk_pixbuf_get_height (tray_pixbuf);
	gint size = MAX (1, MIN (width, height) / 3);
	gint left = width - size, top = height - size;
	GdkPixbuf *dot = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, size, size);
	guchar *pixels = gdk_pixbuf_get_pixels (dot);
	gint stride = gdk_pixbuf_get_rowstride (dot);
	gdouble radius = size / 2.0;
	gdouble inner = MAX (0.0, radius - MAX (0.75, size / 10.0));
	gint x, y, sx, sy;
	/* Supersample a round amber dot with a dark rim, so it remains legible on
	   light panels too. Composite copies; never alter the application's icon. */
	for (y = 0; y < size; y++)
		for (x = 0; x < size; x++)
		{
			guint outer_samples = 0, inner_samples = 0;
			for (sy = 0; sy < 4; sy++)
				for (sx = 0; sx < 4; sx++)
				{
					gdouble dx = x + (sx + 0.5) / 4 - radius;
					gdouble dy = y + (sy + 0.5) / 4 - radius;
					gdouble distance = dx * dx + dy * dy;
					outer_samples += distance < radius * radius;
					inner_samples += distance < inner * inner;
				}
			guchar *pixel = pixels + y * stride + x * 4;
			pixel[0] = outer_samples ? 96 + 159 * inner_samples / outer_samples : 0;
			pixel[1] = outer_samples ? 60 + 100 * inner_samples / outer_samples : 0;
			pixel[2] = 0;
			pixel[3] = 255 * outer_samples / 16;
		}
	guint i;
	for (i = 0; i < G_N_ELEMENTS (attention_frames); i++)
	{
		attention_frames[i] = gdk_pixbuf_copy (tray_pixbuf);
		gdk_pixbuf_composite (dot, attention_frames[i], left, top, size, size,
			left, top, 1.0, 1.0, GDK_INTERP_NEAREST, 115 + 35 * i);
	}
	g_object_unref (dot);
	if (indicator != NULL)
	{
		/* Indicators accept icon names/paths, not pixbufs. Cache each frame in
		   an owner-only temporary file, with distinct names for host caches.
		   No file I/O is needed during animation. */
		for (i = 0; i < G_N_ELEMENTS (attention_paths); i++)
		{
			gint fd = g_file_open_tmp ("emelfm2-attention-XXXXXX.png",
				&attention_paths[i], NULL);
			if (fd < 0)
				break;
			close (fd);
			if (!gdk_pixbuf_save (attention_frames[i], attention_paths[i], "png", NULL, NULL))
				break;
		}
		if (i != G_N_ELEMENTS (attention_paths))
		{
			/* Keep the standard icon and pending label if storage is unavailable. */
			for (i = 0; i < G_N_ELEMENTS (attention_paths); i++)
			{
				if (attention_paths[i] != NULL)
				{
					g_unlink (attention_paths[i]);
					g_free (attention_paths[i]);
					attention_paths[i] = NULL;
				}
			}
		}
	}
}

static void _e2_tray_update_icon (void)
{
	guint frame = pulse_frames[attention_tick % G_N_ELEMENTS (pulse_frames)];
	if (status_icon != NULL)
	{
		if (tray_pixbuf != NULL)
			gtk_status_icon_set_from_pixbuf (status_icon,
				attention_active && attention_frames[frame] != NULL
				? attention_frames[frame] : tray_pixbuf);
		else
			gtk_status_icon_set_from_icon_name (status_icon, BINNAME);
	}
	if (indicator != NULL)
	{
		const gchar *path = attention_active && attention_paths[frame] != NULL
			? attention_paths[frame] : tray_icon_path;
		/* Set both: indicator hosts and the library's X11 fallback can use
		   different icon properties while NeedsAttention is active. */
		indicator_set_icon (indicator, path != NULL ? path : BINNAME);
		if (indicator_set_attention_icon != NULL)
			indicator_set_attention_icon (indicator, path != NULL ? path : BINNAME);
	}
}

static gboolean _e2_tray_pulse_cb (gpointer unused)
{
	CLOSEBGL
	if (++attention_tick >= 3 * G_N_ELEMENTS (pulse_frames))
		attention_source = 0;
	_e2_tray_update_icon ();
	gboolean repeat = attention_source != 0;
	OPENBGL
	return repeat;
}

static void _e2_tray_sync_attention (void)
{
	gboolean active = tray_mode >= 0 && attention_count != 0
		&& e2_option_bool_get ("tray-attention");
	if (active)
		_e2_tray_prepare_attention ();
	if (active != attention_active)
	{
		if (attention_source != 0)
			g_source_remove (attention_source);
		attention_source = attention_tick = 0;
		/* Pulse once per continuous batch, not on each window/menu update or
		   as more questions arrive. The dot then stays until all are answered. */
		if (active && (status_icon != NULL ? attention_frames[0] != NULL
			: attention_paths[0] != NULL))
			attention_source = g_timeout_add (200, _e2_tray_pulse_cb, NULL);
		attention_active = active;
	}
	_e2_tray_update_icon ();
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
		if (mode >= 0)
		{
			_e2_tray_load_icons ();
			e2_tray_set_attention (attention_count);
		}
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
		e2_output_print_error (_("XFCE/GNOME tray support requires libdbusmenu-glib, or an AppIndicator library matching this application's GTK version."), FALSE);
		return;
	}

	tray_mode = mode;
	_e2_tray_load_icons ();
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
		e2_tray_set_attention (attention_count);
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
		e2_tray_set_attention (attention_count);
	}
	e2_tray_windows_sync (TRUE);
#endif
}

void e2_tray_cleanup (void)
{
#ifdef USE_GTK2_10
	if (attention_source != 0)
		g_source_remove (attention_source);
	attention_source = attention_tick = 0;
	attention_active = FALSE;
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
		if (indicator_stop != NULL) indicator_stop (indicator);
		g_object_unref (indicator);
		indicator = NULL;
	}
	if (tray_menu != NULL)
	{
		gtk_widget_destroy (tray_menu);
		g_object_unref (tray_menu);
		tray_menu = NULL;
	}
	_e2_tray_free_icons ();
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
	_e2_tray_sync_attention ();
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
		if (indicator_update_menu != NULL)
			indicator_update_menu (indicator, GTK_MENU (tray_menu));
		indicator_set_status (indicator, attention_active ? 2 : 1);
		if (indicator_set_label != NULL)
		{
			gchar *label = attention_active ? g_strdup_printf ("! %u", count) : g_strdup ("");
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
