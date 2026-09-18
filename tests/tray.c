/* Exercise production tray callbacks and a real AppIndicator D-Bus export. */
#include "../src/e2_tray.c"
#include <gio/gio.h>
#include <glib/gstdio.h>

E2_MainData app;
static gboolean enabled;
static gint mode;
static guint quit_count;
static gchar *icon_path;
static GdkPixbuf *app_icon;
static guint registrations;
static gchar *item_name, *item_path;

gboolean e2_option_bool_get (gchar *name) { return enabled; }
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
	g_assert_true (gtk_widget_get_visible (app.main_window));
	quit_count++;
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

static void test_indicator (void)
{
	if (!_e2_tray_load_indicator ())
	{
		g_test_skip ("Matching AppIndicator runtime is not installed");
		return;
	}
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
		g_signal_emit_by_name (indicator, "connection-changed", FALSE);
		g_assert_true (gtk_widget_get_visible (app.main_window));
		menu_activate (0);
		enabled = FALSE;
		e2_tray_sync ();
		g_assert_true (gtk_widget_get_visible (app.main_window));
		g_assert_null (indicator);
		drain ();
	}
	g_bus_unown_name (owner);
	g_dbus_connection_unregister_object (connection, object);
	g_dbus_node_info_unref (info);
	g_object_unref (connection);
}

int main (int argc, char **argv)
{
	gtk_init (&argc, &argv);
	g_test_init (&argc, &argv, NULL);
	app.main_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_widget_show (app.main_window);
	app_icon = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, 32, 32);
	gdk_pixbuf_fill (app_icon, 0x336699ff);
	icon_path = g_build_filename (g_get_tmp_dir (), "emelfm2-tray-test.png", NULL);
	g_assert_true (gdk_pixbuf_save (app_icon, icon_path, "png", NULL, NULL));
	g_test_add_func ("/tray/x11", test_x11);
	g_test_add_func ("/tray/indicator", test_indicator);
	gint result = g_test_run ();
	e2_tray_cleanup ();
	gtk_widget_destroy (app.main_window);
	g_object_unref (app_icon);
	g_remove (icon_path);
	g_free (icon_path);
	g_free (item_name);
	g_free (item_path);
	return result;
}
