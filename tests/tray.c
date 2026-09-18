/* Exercise tray callbacks, X11 docking and the real desktop D-Bus protocols. */
#include "../src/e2_tray.c"
#include "../src/e2_tray_indicator.c"
#include "../src/e2_tray_windows.c"
#include "../src/e2_tray_notify.c"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <sys/wait.h>
#include <signal.h>
#include "e2_dialog.h"
#ifdef GDK_WINDOWING_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

E2_MainData app;
pthread_mutex_t display_mutex = PTHREAD_MUTEX_INITIALIZER;
void e2_main_close_uilock (void) { pthread_mutex_lock (&display_mutex); }
void e2_main_open_uilock (void) { pthread_mutex_unlock (&display_mutex); }

ViewInfo *curr_view;
void printd_raw (gint level, gchar *file, gint line, const gchar *format, ...) {}
gint e2_option_int_get (gchar *name) { return GTK_WIN_POS_NONE; }
E2_MainLoop *e2_main_loop_new (gboolean maincontext)
{
	E2_MainLoop *loop = g_new0 (E2_MainLoop, 1);
	loop->maincontext = maincontext;
	return loop;
}
void e2_main_loop_quit (E2_MainLoop *loop) { loop->finished = TRUE; }
void e2_main_loop_run (E2_MainLoop *loop)
{
	OPENBGL
	pthread_cleanup_push (g_free, loop);
	while (!loop->finished)
	{
		if (loop->maincontext)
			g_main_context_iteration (NULL, TRUE);
		else
			g_usleep (1000);
	}
	pthread_cleanup_pop (0);
	CLOSEBGL
	g_free (loop);
}
static gboolean enabled;
static gboolean notifications;
static gboolean animate_attention = TRUE;
static gint mode;
static guint quit_count;
static gboolean quit_again;
static gchar *icon_path;
static GdkPixbuf *app_icon;
static guint registrations;
static gchar *item_name, *item_path;

gboolean e2_option_bool_get (gchar *name)
{
	if (!strcmp (name, "tray-notifications")) return notifications;
	if (!strcmp (name, "tray-attention")) return animate_attention;
	return enabled;
}
gint e2_option_sel_get (gchar *name) { return mode; }
GList *e2_icons_get_application (void)
{
	return g_list_append (NULL, g_object_ref (app_icon));
}
gchar *e2_icons_get_application_path (void) { return g_strdup (icon_path); }
void e2_output_print_error (gchar *message, gboolean free_message)
{
	g_error ("%s", message);
}
gboolean e2_main_closedown (gboolean compulsory, gboolean save, gboolean doexit)
{
	g_assert_false (compulsory);
	g_assert_true (save && doexit);
	g_assert_false (gtk_widget_get_visible (app.main_window));
	quit_count++;
	if (quit_again)
	{
		quit_again = FALSE;
		_e2_tray_quit_cb (NULL, NULL);
	}
	return FALSE; /* Simulate cancelling the running-process warning. */
}

static void drain (void)
{
	gint64 end = g_get_monotonic_time () + 100000;
	do
	{
		while (g_main_context_iteration (NULL, FALSE));
		g_usleep (1000);
	} while (g_get_monotonic_time () < end);
}

static void menu_activate (guint index)
{
	GList *items = gtk_container_get_children (GTK_CONTAINER (tray_menu));
	gtk_menu_item_activate (GTK_MENU_ITEM (g_list_nth_data (items, index)));
	g_list_free (items);
}

static void test_x11 (void)
{
	enabled = FALSE;
	e2_tray_sync ();
	g_assert_null (status_icon);
	enabled = TRUE;
	mode = E2_TRAY_X11;
	e2_tray_sync ();
	g_assert_nonnull (status_icon);
	g_assert_true (gtk_status_icon_get_pixbuf (status_icon) == app_icon);
	g_signal_emit_by_name (status_icon, "activate");
	g_assert_false (gtk_widget_get_visible (app.main_window));
	g_signal_emit_by_name (status_icon, "activate");
	g_assert_true (gtk_widget_get_visible (app.main_window));
	g_signal_emit_by_name (status_icon, "popup-menu", 3, GDK_CURRENT_TIME);
	g_assert_true (gtk_widget_get_visible (tray_menu));
	gtk_menu_popdown (GTK_MENU (tray_menu));
	menu_activate (0);
	g_assert_false (gtk_widget_get_visible (app.main_window));
	menu_activate (2);
	g_assert_cmpuint (quit_count, ==, 1);
	g_assert_nonnull (status_icon);
	g_assert_false (gtk_widget_get_visible (app.main_window));
	menu_activate (0);
	enabled = FALSE;
	e2_tray_sync ();
	g_assert_true (gtk_widget_get_visible (app.main_window));
	g_assert_null (status_icon);
	g_assert_null (tray_menu);
	enabled = TRUE;
	e2_tray_sync ();
	menu_activate (0);
	g_object_notify (G_OBJECT (status_icon), "embedded");
	g_assert_true (gtk_widget_get_visible (app.main_window));
	e2_tray_cleanup ();
	e2_tray_cleanup ();
}

static void test_x11_docked (void)
{
#ifdef GDK_WINDOWING_X11
	/* A separate X connection acts as the desktop's tray host. A real docked
	   GtkTrayIcon is a GtkPlug in GTK's toplevel list, not an app dialog. */
	Display *display = XOpenDisplay (NULL);
	g_assert_nonnull (display);
	Window host = XCreateSimpleWindow (display, DefaultRootWindow (display),
		0, 0, 64, 64, 0, 0, 0);
	gchar *selection_name = g_strdup_printf ("_NET_SYSTEM_TRAY_S%d", DefaultScreen (display));
	Atom selection = XInternAtom (display, selection_name, False);
	g_free (selection_name);
	Atom opcode = XInternAtom (display, "_NET_SYSTEM_TRAY_OPCODE", False);
	XSetSelectionOwner (display, selection, host, CurrentTime);
	XMapWindow (display, host);
	XSync (display, False);
	gint animation;
	for (animation = 0; animation <= 1; animation++)
	{
		animate_attention = animation;
		enabled = TRUE;
		mode = E2_TRAY_X11;
		e2_tray_sync ();
		Window docked = None;
		gint64 deadline = g_get_monotonic_time () + 3000000;
		while (docked == None && g_get_monotonic_time () < deadline)
		{
			drain ();
			while (XPending (display))
			{
				XEvent event;
				XNextEvent (display, &event);
				if (event.type == ClientMessage && event.xclient.message_type == opcode
					&& event.xclient.data.l[1] == 0) /* SYSTEM_TRAY_REQUEST_DOCK */
					docked = event.xclient.data.l[2];
			}
		}
		g_assert_cmpuint (docked, !=, None);
		XReparentWindow (display, docked, host, 0, 0);
		XEvent embedded = { 0 };
		embedded.xclient.type = ClientMessage;
		embedded.xclient.window = docked;
		embedded.xclient.message_type = XInternAtom (display, "_XEMBED", False);
		embedded.xclient.format = 32;
		embedded.xclient.data.l[0] = CurrentTime;
		embedded.xclient.data.l[1] = 0; /* XEMBED_EMBEDDED_NOTIFY */
		embedded.xclient.data.l[3] = host;
		XSendEvent (display, docked, False, NoEventMask, &embedded);
		XMapWindow (display, docked);
		XSync (display, False);
		drain ();
		g_assert_true (gtk_status_icon_is_embedded (status_icon));
		Window transient_for;
		g_assert_false (XGetTransientForHint (display, docked, &transient_for));
		XWindowAttributes attributes;
		g_assert_true (XGetWindowAttributes (display, docked, &attributes));
		g_assert_cmpint (attributes.map_state, ==, IsViewable);
		GtkWidget *progress = gtk_dialog_new ();
		e2_tray_register_transfer (progress);
		gtk_widget_show (progress);
		g_signal_emit_by_name (status_icon, "activate");
		drain ();
		g_assert_false (gtk_widget_get_visible (app.main_window));
		g_assert_false (gtk_widget_get_visible (progress));
		g_assert_true (XGetWindowAttributes (display, docked, &attributes));
		g_assert_cmpint (attributes.map_state, ==, IsViewable);
		/* Waiting questions and configuration sync must also leave it docked. */
		GtkWidget *question = gtk_dialog_new ();
		g_assert_true (e2_tray_defer_dialog (question, TRUE, TRUE));
		e2_tray_sync ();
		drain (); drain (); drain ();
		g_assert_cmpuint (attention_count, ==, 1);
		g_assert_false (gtk_widget_get_visible (question));
		g_assert_true (XGetWindowAttributes (display, docked, &attributes));
		g_assert_cmpint (attributes.map_state, ==, IsViewable);
		g_signal_emit_by_name (status_icon, "activate");
		drain ();
		g_assert_true (gtk_widget_get_visible (app.main_window));
		g_assert_true (gtk_widget_get_visible (progress));
		g_assert_false (gtk_widget_get_visible (question));
		g_assert_true (XGetWindowAttributes (display, docked, &attributes));
		g_assert_cmpint (attributes.map_state, ==, IsViewable);
		gtk_widget_destroy (question);
		gtk_widget_destroy (progress);
		enabled = FALSE;
		e2_tray_sync ();
		drain ();
	}
	XDestroyWindow (display, host);
	XCloseDisplay (display);
	drain ();
#else
	g_test_skip ("X11 backend is not available");
#endif
}

static void watcher_call (GDBusConnection *connection, const gchar *sender,
	const gchar *path, const gchar *interface, const gchar *method,
	GVariant *parameters, GDBusMethodInvocation *invocation, gpointer data)
{
	const gchar *service;
	g_variant_get (parameters, "(&s)", &service);
	g_free (item_name);
	g_free (item_path);
	item_name = g_strdup (*service == '/' ? sender : service);
	item_path = g_strdup (*service == '/' ? service : "/StatusNotifierItem");
	registrations++;
	g_dbus_method_invocation_return_value (invocation, NULL);
}

static void test_attention (void)
{
	enabled = TRUE;
	mode = E2_TRAY_X11;
	animate_attention = FALSE;
	e2_tray_sync ();
	menu_activate (0);
	GtkWidget *question = gtk_dialog_new ();
	g_assert_true (e2_tray_defer_dialog (question, TRUE, TRUE));
	drain ();
	g_assert_cmpuint (attention_count, ==, 1);
	g_assert_cmpuint (attention_source, ==, 0);
	g_assert_true (gtk_status_icon_get_pixbuf (status_icon) == app_icon);
	g_assert_true (gtk_widget_get_visible (questions_item));
	/* Enabling attention for an existing question starts a bounded pulse. */
	animate_attention = TRUE;
	e2_tray_sync ();
	guint source = attention_source;
	g_assert_cmpuint (source, !=, 0);
	g_assert_true (gtk_status_icon_get_pixbuf (status_icon) != app_icon);
	gint64 deadline = g_get_monotonic_time () + 7000000;
	while (attention_tick == 0 && g_get_monotonic_time () < deadline)
		drain ();
	g_assert_cmpuint (attention_tick, >, 0);
	g_assert_true (gtk_status_icon_get_pixbuf (status_icon) != attention_frames[4]);
	/* Reapplying options and adding another question must not restart it. */
	guint tick = attention_tick;
	e2_tray_sync ();
	g_assert_cmpuint (attention_tick, ==, tick);
	GtkWidget *second = gtk_dialog_new ();
	g_assert_true (e2_tray_defer_dialog (second, TRUE, TRUE));
	drain ();
	g_assert_cmpuint (attention_count, ==, 2);
	g_assert_cmpuint (attention_source, ==, source);
	g_assert_cmpuint (attention_tick, >=, tick);
	/* Only the round badge area changes, not the source or the whole icon. */
	gint x, y;
	for (y = 0; y < 32; y++)
		for (x = 0; x < 32; x++)
		{
			guchar *original = gdk_pixbuf_get_pixels (app_icon)
				+ y * gdk_pixbuf_get_rowstride (app_icon) + x * 4;
			g_assert_cmpuint (original[0], ==, 0x33);
			g_assert_cmpuint (original[1], ==, 0x66);
			g_assert_cmpuint (original[2], ==, 0x99);
			g_assert_cmpuint (original[3], ==, 0xff);
			if (x < 22 || y < 22 || (x == 31 && y == 31))
			{
				guchar *badged = gdk_pixbuf_get_pixels (attention_frames[4])
					+ y * gdk_pixbuf_get_rowstride (attention_frames[4]) + x * 4;
				g_assert_cmpint (memcmp (original, badged, 4), ==, 0);
			}
		}
	while (attention_source != 0 && g_get_monotonic_time () < deadline)
	{
		drain ();
		g_assert_false (gtk_widget_get_visible (app.main_window));
		g_assert_false (gtk_widget_get_visible (question));
		g_assert_false (gtk_window_get_modal (GTK_WINDOW (question)));
	}
	g_assert_cmpuint (attention_source, ==, 0);
	g_assert_true (gtk_status_icon_get_pixbuf (status_icon) == attention_frames[4]);
	e2_tray_set_attention (2);
	g_assert_cmpuint (attention_source, ==, 0);
	gtk_widget_destroy (second);
	drain ();
	g_assert_true (gtk_status_icon_get_pixbuf (status_icon) == attention_frames[4]);
	g_assert_cmpuint (attention_source, ==, 0);
	gtk_widget_destroy (question);
	drain ();
	g_assert_cmpuint (attention_count, ==, 0);
	g_assert_true (gtk_status_icon_get_pixbuf (status_icon) == app_icon);
	/* A fresh batch can pulse again; changing the toggle stops it immediately. */
	question = gtk_dialog_new ();
	e2_tray_defer_dialog (question, TRUE, TRUE);
	drain ();
	g_assert_cmpuint (attention_source, !=, 0);
	animate_attention = FALSE;
	e2_tray_sync ();
	g_assert_cmpuint (attention_source, ==, 0);
	g_assert_true (gtk_status_icon_get_pixbuf (status_icon) == app_icon);
	g_assert_false (gtk_widget_get_visible (question));
	animate_attention = TRUE;
	e2_tray_sync ();
	g_assert_cmpuint (attention_source, !=, 0);
	/* Disabling the tray cleans a running timer and all cached frames. */
	enabled = FALSE;
	e2_tray_sync ();
	g_assert_cmpuint (attention_source, ==, 0);
	g_assert_null (attention_frames[0]);
	g_assert_false (gtk_widget_get_visible (question));
	gtk_widget_destroy (question);
	drain ();
}

static GVariant *watcher_get (GDBusConnection *connection, const gchar *sender,
	const gchar *path, const gchar *interface, const gchar *property,
	GError **error, gpointer data)
{
	if (!strcmp (property, "IsStatusNotifierHostRegistered"))
		return g_variant_new_boolean (TRUE);
	return g_variant_new_int32 (0);
}

static void call_done (GObject *connection, GAsyncResult *result, gpointer data)
{
	GError *error = NULL;
	*(GVariant**)data = g_dbus_connection_call_finish (G_DBUS_CONNECTION (connection), result, &error);
	g_assert_no_error (error);
}

static GVariant *remote_property (GDBusConnection *connection, const gchar *name)
{
	GVariant *reply = NULL;
	g_dbus_connection_call (connection, item_name, item_path,
		"org.freedesktop.DBus.Properties", "Get",
		g_variant_new ("(ss)", "org.kde.StatusNotifierItem", name),
		G_VARIANT_TYPE ("(v)"), G_DBUS_CALL_FLAGS_NONE, 3000, NULL,
		call_done, &reply);
	while (reply == NULL)
		g_main_context_iteration (NULL, TRUE);
	GVariant *value;
	g_variant_get (reply, "(v)", &value);
	g_variant_unref (reply);
	return value;
}

static gint remote_menu_find (GVariant *node, const gchar *label)
{
	GVariant *properties = g_variant_get_child_value (node, 1);
	const gchar *text;
	gint id = -1;
	if (g_variant_lookup (properties, "label", "&s", &text) && !strcmp (text, label))
		g_variant_get_child (node, 0, "i", &id);
	g_variant_unref (properties);
	GVariant *children = g_variant_get_child_value (node, 2);
	guint i;
	for (i = 0; id == -1 && i < g_variant_n_children (children); i++)
	{
		GVariant *wrapped = g_variant_get_child_value (children, i);
		GVariant *child = g_variant_get_variant (wrapped);
		id = remote_menu_find (child, label);
		g_variant_unref (child);
		g_variant_unref (wrapped);
	}
	g_variant_unref (children);
	return id;
}

static void remote_menu_click (GDBusConnection *connection, const gchar *label)
{
	GVariant *path = remote_property (connection, "Menu"), *reply = NULL;
	g_dbus_connection_call (connection, item_name, g_variant_get_string (path, NULL),
		"com.canonical.dbusmenu", "GetLayout",
		g_variant_new ("(ii@as)", 0, -1, g_variant_new_strv (NULL, 0)), NULL,
		G_DBUS_CALL_FLAGS_NONE, 3000, NULL, call_done, &reply);
	while (reply == NULL) g_main_context_iteration (NULL, TRUE);
	GVariant *root = g_variant_get_child_value (reply, 1);
	gint id = remote_menu_find (root, label);
	g_assert_cmpint (id, >, 0);
	g_variant_unref (root);
	g_variant_unref (reply);
	reply = NULL;
	g_dbus_connection_call (connection, item_name, g_variant_get_string (path, NULL),
		"com.canonical.dbusmenu", "Event",
		g_variant_new ("(isvu)", id, "clicked", g_variant_new_int32 (0), 0), NULL,
		G_DBUS_CALL_FLAGS_NONE, 3000, NULL, call_done, &reply);
	while (reply == NULL) g_main_context_iteration (NULL, TRUE);
	g_variant_unref (reply);
	g_variant_unref (path);
}

static void test_indicator (void)
{
	if (!_e2_tray_load_indicator ())
	{
		g_test_skip ("No D-Bus menu or AppIndicator runtime is installed");
		return;
	}
	if (e2_tray_indicator_available ())
		g_assert_true (indicator_new == e2_tray_indicator_new);
	GError *error = NULL;
	GDBusConnection *connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
	g_assert_no_error (error);
	const gchar *xml = "<node><interface name='org.kde.StatusNotifierWatcher'>"
		"<method name='RegisterStatusNotifierItem'><arg type='s' direction='in'/></method>"
		"<property name='IsStatusNotifierHostRegistered' type='b' access='read'/>"
		"<property name='ProtocolVersion' type='i' access='read'/>"
		"</interface></node>";
	GDBusNodeInfo *info = g_dbus_node_info_new_for_xml (xml, &error);
	g_assert_no_error (error);
	static const GDBusInterfaceVTable vtable = { watcher_call, watcher_get, NULL };
	guint object = g_dbus_connection_register_object (connection,
		"/StatusNotifierWatcher", info->interfaces[0], &vtable, NULL, NULL, &error);
	g_assert_no_error (error);
	guint owner = g_bus_own_name_on_connection (connection,
		"org.kde.StatusNotifierWatcher", G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, NULL, NULL);
	drain ();
	/* Changing backend while hidden must leave a reachable window. */
	enabled = TRUE;
	mode = E2_TRAY_X11;
	e2_tray_sync ();
	menu_activate (0);
	for (mode = E2_TRAY_XFCE; mode <= E2_TRAY_GNOME; mode++)
	{
		guint previous = registrations;
		enabled = TRUE;
		e2_tray_sync ();
		g_assert_nonnull (indicator);
		g_assert_null (status_icon);
		g_assert_true (gtk_widget_get_visible (app.main_window));
		gint64 deadline = g_get_monotonic_time () + 3000000;
		while (registrations == previous && g_get_monotonic_time () < deadline)
			drain ();
		g_assert_cmpuint (registrations, >, previous);
		GVariant *property = remote_property (connection, "IconName");
		g_assert_cmpstr (g_variant_get_string (property, NULL), ==, icon_path);
		g_variant_unref (property);
		property = remote_property (connection, "Status");
		g_assert_cmpstr (g_variant_get_string (property, NULL), ==, "Active");
		g_variant_unref (property);
		property = remote_property (connection, "ItemIsMenu");
		g_assert_true (g_variant_get_boolean (property));
		g_variant_unref (property);
		property = remote_property (connection, "Menu");
		g_assert_cmpstr (g_variant_get_string (property, NULL), !=, "/");
		/* Fetch the exported menu, then activate its first item over D-Bus,
		   just as an XFCE/GNOME indicator host does. */
		const gchar *menu_path = g_variant_get_string (property, NULL);
		GVariant *layout = NULL;
		g_dbus_connection_call (connection, item_name, menu_path,
			"com.canonical.dbusmenu", "GetLayout",
			g_variant_new ("(ii@as)", 0, -1, g_variant_new_strv (NULL, 0)),
			NULL, G_DBUS_CALL_FLAGS_NONE, 3000, NULL, call_done, &layout);
		while (layout == NULL)
			g_main_context_iteration (NULL, TRUE);
		GVariant *root = g_variant_get_child_value (layout, 1);
		GVariant *children = g_variant_get_child_value (root, 2);
		GVariant *wrapped = g_variant_get_child_value (children, 0);
		GVariant *first = g_variant_get_variant (wrapped);
		gint id;
		g_variant_get_child (first, 0, "i", &id);
		GVariant *event_reply = NULL;
		g_dbus_connection_call (connection, item_name, menu_path,
			"com.canonical.dbusmenu", "Event",
			g_variant_new ("(isvu)", id, "clicked", g_variant_new_int32 (0), 0),
			NULL, G_DBUS_CALL_FLAGS_NONE, 3000, NULL, call_done, &event_reply);
		while (event_reply == NULL)
			g_main_context_iteration (NULL, TRUE);
		g_variant_unref (event_reply);
		g_variant_unref (first);
		g_variant_unref (wrapped);
		g_variant_unref (children);
		g_variant_unref (root);
		g_variant_unref (layout);
		g_variant_unref (property);
		g_assert_false (gtk_widget_get_visible (app.main_window));
		guint previous_quits = quit_count;
		remote_menu_click (connection, "_Quit");
		g_assert_cmpuint (quit_count, ==, previous_quits + 1);
		g_assert_false (gtk_widget_get_visible (app.main_window));
		GtkWidget *pending = gtk_dialog_new ();
		gtk_window_set_title (GTK_WINDOW (pending), "Overwrite some_file");
		g_assert_true (e2_tray_defer_dialog (pending, TRUE, TRUE));
		drain ();
		property = remote_property (connection, "Status");
		g_assert_cmpstr (g_variant_get_string (property, NULL), ==, "NeedsAttention");
		g_variant_unref (property);
		property = remote_property (connection, "AttentionIconName");
		gchar *first_frame = g_variant_dup_string (property, NULL);
		g_variant_unref (property);
		g_assert_cmpstr (first_frame, !=, icon_path);
		GdkPixbuf *exported = gdk_pixbuf_new_from_file (first_frame, &error);
		g_assert_no_error (error);
		g_assert_nonnull (exported);
		g_object_unref (exported);
		drain (); drain ();
		property = remote_property (connection, "AttentionIconName");
		g_assert_cmpstr (g_variant_get_string (property, NULL), !=, first_frame);
		g_variant_unref (property);
		/* Disabling only the cue clears both exported icons/status while
		   leaving the question pending and the window hidden. */
		animate_attention = FALSE;
		e2_tray_sync ();
		g_assert_false (g_file_test (first_frame, G_FILE_TEST_EXISTS));
		g_free (first_frame);
		g_assert_cmpuint (attention_source, ==, 0);
		property = remote_property (connection, "Status");
		g_assert_cmpstr (g_variant_get_string (property, NULL), ==, "Active");
		g_variant_unref (property);
		property = remote_property (connection, "IconName");
		g_assert_cmpstr (g_variant_get_string (property, NULL), ==, icon_path);
		g_variant_unref (property);
		property = remote_property (connection, "AttentionIconName");
		g_assert_cmpstr (g_variant_get_string (property, NULL), ==, icon_path);
		g_variant_unref (property);
		g_assert_cmpuint (attention_count, ==, 1);
		g_assert_false (gtk_widget_get_visible (app.main_window));
		animate_attention = TRUE;
		e2_tray_sync ();
		g_assert_cmpuint (attention_source, !=, 0);
		gchar *cached_paths[5];
		guint i;
		for (i = 0; i < G_N_ELEMENTS (cached_paths); i++)
			cached_paths[i] = g_strdup (attention_paths[i]);
		g_assert_false (gtk_widget_get_visible (pending));
		/* Pending submenus are exported too, with literal filename underscores.
		   Only an explicit menu action opens the deferred question. */
		remote_menu_click (connection, "Overwrite some__file");
		g_assert_true (gtk_widget_get_visible (pending));
		g_assert_true (gtk_window_get_modal (GTK_WINDOW (pending)));
		menu_activate (0);
		g_assert_false (gtk_widget_get_visible (pending));
		/* Losing and restarting the real watcher must restore access and
		   register again, without opening pending questions. */
		previous = registrations;
		g_bus_unown_name (owner);
		drain ();
		g_assert_true (gtk_widget_get_visible (app.main_window));
		owner = g_bus_own_name_on_connection (connection,
			"org.kde.StatusNotifierWatcher", G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, NULL, NULL);
		deadline = g_get_monotonic_time () + 3000000;
		while (registrations == previous && g_get_monotonic_time () < deadline)
			drain ();
		g_assert_cmpuint (registrations, >, previous);
		g_assert_false (gtk_widget_get_visible (pending));
		gtk_widget_destroy (pending);
		drain ();
		g_assert_cmpuint (attention_source, ==, 0);
		property = remote_property (connection, "IconName");
		g_assert_cmpstr (g_variant_get_string (property, NULL), ==, icon_path);
		g_variant_unref (property);
		g_assert_true (gtk_widget_get_visible (app.main_window));
		menu_activate (0);
		enabled = FALSE;
		e2_tray_sync ();
		g_assert_true (gtk_widget_get_visible (app.main_window));
		g_assert_null (indicator);
		for (i = 0; i < G_N_ELEMENTS (cached_paths); i++)
		{
			g_assert_false (g_file_test (cached_paths[i], G_FILE_TEST_EXISTS));
			g_free (cached_paths[i]);
		}
		drain ();
	}
	g_bus_unown_name (owner);
	g_dbus_connection_unregister_object (connection, object);
	g_dbus_node_info_unref (info);
	g_object_unref (connection);
}

static GVariant *xfce_applications (GDBusConnection *bus)
{
	GVariant *reply = NULL;
	g_dbus_connection_call (bus, "org.ayatana.indicator.application",
		"/org/ayatana/indicator/application/service", "org.ayatana.indicator.application.service",
		"GetApplications", NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 3000, NULL, call_done, &reply);
	while (reply == NULL) g_main_context_iteration (NULL, TRUE);
	GVariant *applications = g_variant_get_child_value (reply, 0);
	g_variant_unref (reply);
	return applications;
}

static void assert_xfce_registered (GDBusConnection *bus, guint count)
{
	GVariant *reply = NULL;
	g_dbus_connection_call (bus, "org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher",
		"org.freedesktop.DBus.Properties", "Get",
		g_variant_new ("(ss)", "org.kde.StatusNotifierWatcher", "RegisteredStatusNotifierItems"),
		NULL, G_DBUS_CALL_FLAGS_NONE, 3000, NULL, call_done, &reply);
	while (reply == NULL) g_main_context_iteration (NULL, TRUE);
	GVariant *items;
	g_variant_get (reply, "(v)", &items);
	g_assert_cmpuint (g_variant_n_children (items), ==, count);
	g_variant_unref (items);
	g_variant_unref (reply);
}

static void test_xfce_service (void)
{
	const gchar *service = "/usr/libexec/ayatana-indicator-application/ayatana-indicator-application-service";
	if (!g_file_test (service, G_FILE_TEST_IS_EXECUTABLE) || !e2_tray_indicator_available ())
	{
		g_test_skip ("XFCE's application indicator service is not installed");
		return;
	}
	/* This is the actual service used by xfce4-indicator-plugin, running on
	   the test's private bus. It validates our item and publishes the icon
	   and menu to the panel, even when this test is compiled with GTK 2. */
	GError *error = NULL;
	GPid pid;
	gchar *argv[] = { (gchar*)service, NULL };
	g_assert_true (g_spawn_async (NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD,
		NULL, NULL, &pid, &error));
	g_assert_no_error (error);
	GDBusConnection *bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
	g_assert_no_error (error);
	gboolean ready = FALSE;
	gint64 deadline = g_get_monotonic_time () + 5000000;
	while (!ready && g_get_monotonic_time () < deadline)
	{
		GVariant *reply = g_dbus_connection_call_sync (bus, "org.freedesktop.DBus",
			"/org/freedesktop/DBus", "org.freedesktop.DBus", "NameHasOwner",
			g_variant_new ("(s)", "org.ayatana.indicator.application"), NULL,
			G_DBUS_CALL_FLAGS_NONE, 1000, NULL, &error);
		g_assert_no_error (error);
		g_variant_get (reply, "(b)", &ready);
		g_variant_unref (reply);
		drain ();
	}
	g_assert_true (ready);
	for (mode = E2_TRAY_XFCE; mode <= E2_TRAY_GNOME; mode++)
	{
		enabled = TRUE;
		e2_tray_sync ();
		g_assert_true (indicator_new == e2_tray_indicator_new);
		GVariant *applications = NULL;
		deadline = g_get_monotonic_time () + 5000000;
		do
		{
			if (applications != NULL) g_variant_unref (applications);
			drain ();
			applications = xfce_applications (bus);
		} while (g_variant_n_children (applications) == 0 && g_get_monotonic_time () < deadline);
		g_assert_cmpuint (g_variant_n_children (applications), ==, 1);
		assert_xfce_registered (bus, 1);
		GVariant *entry = g_variant_get_child_value (applications, 0);
		const gchar *icon, *name, *menu, *hint;
		g_variant_get_child (entry, 0, "&s", &icon);
		g_variant_get_child (entry, 2, "&s", &name);
		g_variant_get_child (entry, 3, "&o", &menu);
		g_variant_get_child (entry, 8, "&s", &hint);
		g_assert_cmpstr (icon, ==, icon_path);
		g_assert_cmpstr (hint, ==, BINNAME);
		GVariant *layout = NULL;
		g_dbus_connection_call (bus, name, menu, "com.canonical.dbusmenu", "GetLayout",
			g_variant_new ("(ii@as)", 0, -1, g_variant_new_strv (NULL, 0)), NULL,
			G_DBUS_CALL_FLAGS_NONE, 3000, NULL, call_done, &layout);
		while (layout == NULL) g_main_context_iteration (NULL, TRUE);
		g_variant_unref (layout);
		g_variant_unref (entry);
		g_variant_unref (applications);
		enabled = FALSE;
		e2_tray_sync ();
		drain ();
		applications = xfce_applications (bus);
		g_assert_cmpuint (g_variant_n_children (applications), ==, 0);
		assert_xfce_registered (bus, 0);
		g_variant_unref (applications);
	}
	/* Disable before the async connection callback, then let it complete.
	   A cancelled setup must not leave a ghost entry in the desktop panel. */
	enabled = TRUE;
	mode = E2_TRAY_XFCE;
	e2_tray_sync ();
	enabled = FALSE;
	e2_tray_sync ();
	drain ();
	GVariant *applications = xfce_applications (bus);
	g_assert_cmpuint (g_variant_n_children (applications), ==, 0);
	g_variant_unref (applications);
	kill (pid, SIGTERM);
	waitpid (pid, NULL, 0);
	g_spawn_close_pid (pid);
	g_object_unref (bus);
	drain ();
}

static guint question_maps, question_responses;
static void question_mapped (GtkWidget *widget, gpointer unused) { question_maps++; }
static void question_answered (GtkDialog *widget, gint response, gpointer unused) { question_responses++; }

static GtkWidget *new_question (const gchar *title)
{
	GtkWidget *dialog = gtk_dialog_new ();
	gtk_window_set_title (GTK_WINDOW (dialog), title);
	g_signal_connect (dialog, "map", G_CALLBACK (question_mapped), NULL);
	g_signal_connect (dialog, "response", G_CALLBACK (question_answered), NULL);
	return dialog;
}

static void count_window_maps (GtkWidget *widget, guint *count) { (*count)++; }

typedef struct
{
	GtkWidget *dialog, *progress, *pending;
	gint response;
} QuitDialogTest;

static gboolean answer_quit_dialog (gpointer data)
{
	QuitDialogTest *test = data;
	g_assert_true (gtk_widget_get_visible (test->dialog));
	g_assert_true (gtk_window_get_modal (GTK_WINDOW (test->dialog)));
	g_assert_null (gtk_window_get_transient_for (GTK_WINDOW (test->dialog)));
	g_assert_false (gtk_window_get_skip_taskbar_hint (GTK_WINDOW (test->dialog)));
	g_assert_false (gtk_widget_get_visible (app.main_window));
	g_assert_false (gtk_widget_get_visible (test->progress));
	g_assert_false (gtk_widget_get_visible (test->pending));
	g_assert_true (e2_tray_is_hidden ());
	/* Rescanning windows must not hide the quit prompt or queue it behind
	   a background question. Its wait ends only on the explicit response. */
	e2_tray_windows_sync (TRUE);
	e2_tray_windows_hide ();
	g_assert_true (gtk_widget_get_visible (test->dialog));
	g_assert_false (e2_tray_defer_dialog (test->dialog, TRUE, TRUE));
	gtk_dialog_response (GTK_DIALOG (test->dialog), test->response);
	return FALSE;
}

static void test_quit_hidden (void)
{
	enabled = TRUE;
	mode = E2_TRAY_X11;
	e2_tray_sync ();
	GtkWidget *progress = gtk_dialog_new ();
	e2_tray_register_transfer (progress);
	gtk_widget_show (progress);
	menu_activate (0);
	guint maps = 0, previous = quit_count;
	gulong main_handler = g_signal_connect (app.main_window, "map", G_CALLBACK (count_window_maps), &maps);
	gulong progress_handler = g_signal_connect (progress, "map", G_CALLBACK (count_window_maps), &maps);
	quit_again = TRUE;
	menu_activate (2);
	g_assert_cmpuint (quit_count, ==, previous + 1);
	g_assert_cmpuint (maps, ==, 0);
	g_assert_true (e2_tray_is_hidden ());
	GtkWidget *pending = gtk_dialog_new ();
	g_assert_true (e2_tray_defer_dialog (pending, TRUE, TRUE));
	drain ();
	/* Exercise the real dialog setup/wait/destruction used by shutdown,
	   with both Continue and Quit responses and another pending question. */
	const gint responses[] = { GTK_RESPONSE_YES, GTK_RESPONSE_NO };
	guint i;
	for (i = 0; i < G_N_ELEMENTS (responses); i++)
	{
		GtkWidget *dialog = gtk_dialog_new ();
		e2_tray_prepare_quit_dialog (dialog);
		e2_dialog_setup (dialog, NULL);
		QuitDialogTest test = { dialog, progress, pending, responses[i] };
		g_timeout_add (50, answer_quit_dialog, &test);
		CLOSEBGL
		DialogButtons result = e2_dialog_run (dialog, NULL, E2_DIALOG_BLOCKED | E2_DIALOG_FREE);
		OPENBGL
		g_assert_cmpint (result, ==, i == 0 ? OK : CANCEL);
		drain ();
		g_assert_cmpuint (maps, ==, 0);
		g_assert_cmpuint (attention_count, ==, 1);
		g_assert_true (e2_tray_is_hidden ());
		g_assert_false (gtk_widget_get_visible (pending));
	}
	/* Cancelling shutdown leaves the tray available for another Quit. */
	menu_activate (2);
	g_assert_cmpuint (quit_count, ==, previous + 2);
	g_assert_cmpuint (maps, ==, 0);
	g_signal_handler_disconnect (app.main_window, main_handler);
	g_signal_handler_disconnect (progress, progress_handler);
	gtk_widget_destroy (pending);
	gtk_widget_destroy (progress);
	enabled = FALSE;
	e2_tray_sync ();
	drain ();
}

static void test_deferred_windows (void)
{
	enabled = TRUE;
	mode = E2_TRAY_X11;
	e2_tray_sync ();
	GtkWidget *progress = gtk_dialog_new ();
	GtkWidget *already_hidden = gtk_dialog_new ();
	e2_tray_register_transfer (progress);
	e2_tray_register_window (already_hidden);
	gtk_widget_show (progress);
	g_assert_true (gtk_window_get_skip_taskbar_hint (GTK_WINDOW (progress)));
	g_assert_true (gtk_window_get_transient_for (GTK_WINDOW (progress)) == GTK_WINDOW (app.main_window));
	menu_activate (0);
	g_assert_false (gtk_widget_get_visible (progress));
	GtkWidget *later = gtk_dialog_new ();
	e2_tray_register_transfer (later);
	g_assert_true (e2_tray_defer_dialog (later, FALSE, TRUE));
	GtkWidget *first = new_question ("Copy: overwrite confirmation");
	GtkWidget *second = new_question ("Move: overwrite confirmation");
	g_assert_true (e2_tray_defer_dialog (first, TRUE, TRUE));
	g_assert_true (e2_tray_defer_dialog (second, TRUE, TRUE));
	drain ();
	g_assert_cmpuint (attention_count, ==, 2);
	g_assert_cmpuint (question_maps, ==, 0);
	g_assert_cmpuint (question_responses, ==, 0);
	g_assert_false (gtk_window_get_modal (GTK_WINDOW (first)));
	g_assert_false (gtk_widget_get_visible (app.main_window));
	g_assert_cmpstr (gtk_label_get_text (GTK_LABEL (banner_label)), ==, "Needs attention (2)");
	g_assert_true (gtk_widget_get_visible (questions_item));
	g_assert_true (gtk_widget_get_visible (transfers_item));
	g_assert_null (notification_bus);
	/* Merely restoring the application must not open a pending question. */
	menu_activate (0);
	g_assert_true (gtk_widget_get_visible (progress));
	g_assert_true (gtk_widget_get_visible (later));
	g_assert_false (gtk_widget_get_visible (already_hidden));
	g_assert_false (gtk_widget_get_visible (first));
	g_assert_false (gtk_widget_get_visible (second));
	g_assert_true (gtk_widget_get_visible (banner));
	GList *questions = gtk_container_get_children (GTK_CONTAINER (
		gtk_menu_item_get_submenu (GTK_MENU_ITEM (questions_item))));
	gtk_menu_item_activate (GTK_MENU_ITEM (questions->data));
	g_list_free (questions);
	g_assert_true (gtk_widget_get_visible (first));
	g_assert_true (gtk_window_get_modal (GTK_WINDOW (first)));
	g_assert_false (gtk_widget_get_visible (second));
	/* Hiding an open modal question removes its grab and queues it again. */
	menu_activate (0);
	g_assert_false (gtk_widget_get_visible (first));
	g_assert_false (gtk_window_get_modal (GTK_WINDOW (first)));
	g_assert_true (gtk_grab_get_current () != first);
	e2_tray_review_pending ();
	g_assert_false (gtk_widget_get_visible (progress));
	g_assert_false (gtk_widget_get_visible (later));
	g_assert_true (e2_tray_defer_dialog (progress, FALSE, FALSE));
	gtk_dialog_response (GTK_DIALOG (first), GTK_RESPONSE_NO);
	gtk_widget_destroy (first);
	drain ();
	g_assert_cmpuint (question_responses, ==, 1);
	g_assert_cmpuint (attention_count, ==, 1);
	g_assert_false (gtk_widget_get_visible (second));
	/* Disabling tray restores ordinary windows, not unanswered questions. */
	menu_activate (0);
	enabled = FALSE;
	e2_tray_sync ();
	drain ();
	g_assert_true (gtk_widget_get_visible (app.main_window));
	g_assert_true (gtk_widget_get_visible (progress));
	g_assert_false (gtk_window_get_skip_taskbar_hint (GTK_WINDOW (progress)));
	g_assert_false (gtk_widget_get_visible (second));
	g_assert_true (gtk_widget_get_visible (banner));
	/* Cancellation/destruction clears attention without inventing an answer. */
	gtk_widget_destroy (second);
	gtk_widget_destroy (later);
	gtk_widget_destroy (progress);
	gtk_widget_destroy (already_hidden);
	drain ();
	g_assert_cmpuint (attention_count, ==, 0);
	g_assert_false (gtk_widget_get_visible (banner));
	g_assert_cmpuint (question_responses, ==, 1);
	e2_tray_cleanup ();
}

static gboolean answer_deferred (gpointer data)
{
	GtkWidget *dialog = data;
	g_assert_true (e2_tray_is_hidden ());
	g_assert_false (gtk_widget_get_visible (dialog));
	g_assert_false (gtk_window_get_modal (GTK_WINDOW (dialog)));
	e2_tray_show_main ();
	g_assert_false (gtk_widget_get_visible (dialog));
	e2_tray_review_pending ();
	g_assert_true (gtk_widget_get_visible (dialog));
	g_assert_true (gtk_window_get_modal (GTK_WINDOW (dialog)));
	gtk_dialog_response (GTK_DIALOG (dialog), GTK_RESPONSE_NO);
	return FALSE;
}

static gboolean destroy_deferred (gpointer dialog)
{
	g_assert_false (gtk_widget_get_visible (dialog));
	gtk_widget_destroy (dialog);
	return FALSE;
}

static void test_dialog_wait (void)
{
	enabled = TRUE;
	mode = E2_TRAY_X11;
	e2_tray_sync ();
	menu_activate (0);
	GtkWidget *dialog = new_question ("Overwrite confirmation");
	g_timeout_add (50, answer_deferred, dialog);
	/* Exercise production presentation and wait code with a synchronous
	   main-loop adapter. Nothing completes until an explicit response. */
	CLOSEBGL
	DialogButtons result = e2_dialog_run (dialog, app.main_window, E2_DIALOG_BLOCKED);
	OPENBGL
	g_assert_cmpint (result, ==, CANCEL);
	gtk_widget_destroy (dialog);
	menu_activate (0);
	dialog = new_question ("Cancelled operation");
	g_timeout_add (50, destroy_deferred, dialog);
	CLOSEBGL
	result = e2_dialog_run (dialog, app.main_window, E2_DIALOG_BLOCKED);
	OPENBGL
	g_assert_cmpint (result, ==, CANCEL);
	drain ();
	g_assert_cmpuint (attention_count, ==, 0);
	enabled = FALSE;
	e2_tray_sync ();
}

static gpointer wait_in_task (gpointer dialog)
{
	e2_dialog_run (dialog, app.main_window, E2_DIALOG_BLOCKED | E2_DIALOG_CLOSELOCK);
	return NULL;
}

static void test_cancelled_task (void)
{
	enabled = TRUE;
	mode = E2_TRAY_X11;
	e2_tray_sync ();
	menu_activate (0);
	GtkWidget *dialog = new_question ("Cancelled copy question");
	g_object_ref (dialog);
	pthread_t thread;
	g_assert_cmpint (pthread_create (&thread, NULL, wait_in_task, dialog), ==, 0);
	drain ();
	g_assert_cmpuint (attention_count, ==, 1);
	g_assert_cmpint (pthread_cancel (thread), ==, 0);
	gpointer result;
	g_assert_cmpint (pthread_join (thread, &result), ==, 0);
	g_assert_true (result == PTHREAD_CANCELED);
	drain ();
	g_assert_cmpuint (attention_count, ==, 0);
	g_assert_null (_e2_tray_window (dialog));
	g_object_unref (dialog);
	enabled = FALSE;
	e2_tray_sync ();
}

static guint notifications_received, notifications_closed;
static GDBusMethodInvocation *delayed_notification;
static gboolean delay_notification;

static void notification_call (GDBusConnection *connection, const gchar *sender,
	const gchar *path, const gchar *interface, const gchar *method,
	GVariant *parameters, GDBusMethodInvocation *invocation, gpointer data)
{
	if (!strcmp (method, "CloseNotification"))
	{
		notifications_closed++;
		g_dbus_method_invocation_return_value (invocation, NULL);
		return;
	}
	notifications_received++;
	GVariant *actions = g_variant_get_child_value (parameters, 5);
	g_assert_cmpuint (g_variant_n_children (actions), ==, 2);
	const gchar *action;
	g_variant_get_child (actions, 0, "&s", &action);
	g_assert_cmpstr (action, ==, "review");
	g_variant_get_child (actions, 1, "&s", &action);
	g_assert_cmpstr (action, ==, _("Review"));
	g_variant_unref (actions);
	GVariant *hints = g_variant_get_child_value (parameters, 6);
	gboolean silent = FALSE;
	g_assert_true (g_variant_lookup (hints, "suppress-sound", "b", &silent));
	g_assert_true (silent);
	g_variant_unref (hints);
	if (delay_notification)
		delayed_notification = g_object_ref (invocation);
	else
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(u)", 42));
}

static void test_notifications (void)
{
	GError *error = NULL;
	GDBusConnection *bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
	g_assert_no_error (error);
	const gchar *xml = "<node><interface name='org.freedesktop.Notifications'>"
		"<method name='Notify'><arg type='s' direction='in'/><arg type='u' direction='in'/>"
		"<arg type='s' direction='in'/><arg type='s' direction='in'/><arg type='s' direction='in'/>"
		"<arg type='as' direction='in'/><arg type='a{sv}' direction='in'/><arg type='i' direction='in'/>"
		"<arg type='u' direction='out'/></method>"
		"<method name='CloseNotification'><arg type='u' direction='in'/></method>"
		"<signal name='ActionInvoked'><arg type='u'/><arg type='s'/></signal>"
		"</interface></node>";
	GDBusNodeInfo *info = g_dbus_node_info_new_for_xml (xml, &error);
	g_assert_no_error (error);
	static const GDBusInterfaceVTable vtable = { notification_call, NULL, NULL };
	guint object = g_dbus_connection_register_object (bus, "/org/freedesktop/Notifications",
		info->interfaces[0], &vtable, NULL, NULL, &error);
	g_assert_no_error (error);
	guint owner = g_bus_own_name_on_connection (bus, "org.freedesktop.Notifications",
		G_BUS_NAME_OWNER_FLAGS_NONE, NULL, NULL, NULL, NULL);
	drain ();
	enabled = TRUE;
	mode = E2_TRAY_X11;
	e2_tray_sync ();
	menu_activate (0);
	GtkWidget *first = new_question ("Copy question");
	GtkWidget *second = new_question ("Move question");
	e2_tray_defer_dialog (first, TRUE, TRUE);
	drain (); drain (); drain ();
	g_assert_cmpuint (notifications_received, ==, 0); /* default is disabled */
	notifications = TRUE;
	e2_tray_sync ();
	drain (); drain (); drain ();
	g_assert_cmpuint (notifications_received, ==, 1);
	g_assert_cmpuint (notification_id, ==, 42);
	g_assert_false (gtk_widget_get_visible (app.main_window));
	e2_tray_defer_dialog (second, TRUE, TRUE);
	drain (); drain (); drain ();
	g_assert_cmpuint (notifications_received, ==, 1); /* no repeated reminders */
	/* Dismissal does not answer the question. Only Review may reveal it. */
	g_dbus_connection_emit_signal (bus, NULL, "/org/freedesktop/Notifications",
		"org.freedesktop.Notifications", "ActionInvoked",
		g_variant_new ("(us)", 42, "review"), &error);
	g_assert_no_error (error);
	drain ();
	g_assert_true (gtk_widget_get_visible (first));
	g_assert_false (gtk_widget_get_visible (second));
	gtk_widget_destroy (first);
	gtk_widget_destroy (second);
	drain ();
	g_assert_cmpuint (attention_count, ==, 0);
	/* A response arriving after notifications are disabled must be withdrawn. */
	delay_notification = TRUE;
	menu_activate (0);
	first = new_question ("Late question");
	e2_tray_defer_dialog (first, TRUE, TRUE);
	drain (); drain (); drain ();
	g_assert_nonnull (delayed_notification);
	notifications = FALSE;
	e2_tray_sync ();
	drain ();
	guint previous = notifications_closed;
	g_dbus_method_invocation_return_value (delayed_notification, g_variant_new ("(u)", 43));
	g_object_unref (delayed_notification);
	delayed_notification = NULL;
	drain ();
	g_assert_cmpuint (notifications_closed, ==, previous + 1);
	g_assert_false (gtk_widget_get_visible (first));
	gtk_widget_destroy (first);
	enabled = FALSE;
	e2_tray_sync ();
	drain ();
	g_bus_unown_name (owner);
	g_dbus_connection_unregister_object (bus, object);
	g_dbus_node_info_unref (info);
	g_object_unref (bus);
}

int main (int argc, char **argv)
{
	gtk_init (&argc, &argv);
	g_test_init (&argc, &argv, NULL);
	app.main_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
#ifdef USE_GTK3_0
	app.vbox_main = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
#else
	app.vbox_main = gtk_vbox_new (FALSE, 0);
#endif
	gtk_container_add (GTK_CONTAINER (app.main_window), app.vbox_main);
	curr_view = &app.pane1.view;
	curr_view->treeview = gtk_tree_view_new ();
	gtk_container_add (GTK_CONTAINER (app.vbox_main), curr_view->treeview);
	gtk_widget_show (curr_view->treeview);
	gtk_widget_show (app.vbox_main);
	gtk_widget_show (app.main_window);
	app_icon = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, 32, 32);
	gdk_pixbuf_fill (app_icon, 0x336699ff);
	icon_path = g_build_filename (g_get_tmp_dir (), "emelfm2-tray-test.png", NULL);
	g_assert_true (gdk_pixbuf_save (app_icon, icon_path, "png", NULL, NULL));
	g_test_add_func ("/tray/x11", test_x11);
	g_test_add_func ("/tray/x11-docked", test_x11_docked);
	g_test_add_func ("/tray/attention", test_attention);
	g_test_add_func ("/tray/indicator", test_indicator);
	g_test_add_func ("/tray/xfce-service", test_xfce_service);
	g_test_add_func ("/tray/quit-hidden", test_quit_hidden);
	g_test_add_func ("/tray/deferred-windows", test_deferred_windows);
	g_test_add_func ("/tray/notifications", test_notifications);
	g_test_add_func ("/tray/dialog-wait", test_dialog_wait);
	g_test_add_func ("/tray/cancelled-task", test_cancelled_task);
	gint result = g_test_run ();
	e2_tray_windows_cleanup ();
	e2_tray_cleanup ();
	gtk_widget_destroy (app.main_window);
	g_object_unref (app_icon);
	g_remove (icon_path);
	g_free (icon_path);
	g_free (item_name);
	g_free (item_path);
	return result;
}
