/* Optional, passive desktop notifications. GPL version 3 or later. */
#include "emelfm2.h"
#include "e2_tray.h"
#include "e2_option.h"
#include "e2_icons.h"

#if GLIB_CHECK_VERSION(2,26,0)
#include <gio/gio.h>

static guint notification_id, notification_generation, notification_timer;
static guint notification_count, action_subscription;
static gboolean notification_batch;
static GDBusConnection *notification_bus;

static void _e2_tray_close_notification (GDBusConnection *bus, guint id)
{
	if (id != 0)
		g_dbus_connection_call (bus, "org.freedesktop.Notifications",
			"/org/freedesktop/Notifications", "org.freedesktop.Notifications",
			"CloseNotification", g_variant_new ("(u)", id), NULL,
			G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL, NULL);
}

static void _e2_tray_notification_action (GDBusConnection *bus,
	const gchar *sender, const gchar *path, const gchar *interface,
	const gchar *signal, GVariant *parameters, gpointer unused)
{
	CLOSEBGL
	guint id;
	const gchar *action;
	g_variant_get (parameters, "(u&s)", &id, &action);
	if (id == notification_id && notification_count != 0
		&& (!strcmp (action, "review") || !strcmp (action, "default")))
	{
		_e2_tray_close_notification (bus, notification_id);
		notification_id = 0;
		e2_tray_review_pending ();
	}
	OPENBGL
}

static void _e2_tray_notified (GObject *bus, GAsyncResult *result, gpointer data)
{
	CLOSEBGL
	GError *error = NULL;
	GVariant *reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (bus), result, &error);
	if (reply != NULL)
	{
		guint id;
		g_variant_get (reply, "(u)", &id);
		/* A late reply must not leave a notification behind after disable,
		   cancellation or shutdown. The async call retains its connection. */
		if (GPOINTER_TO_UINT (data) == notification_generation)
			notification_id = id;
		else
			_e2_tray_close_notification (G_DBUS_CONNECTION (bus), id);
		g_variant_unref (reply);
	}
	g_clear_error (&error); /* Tray menu and in-window indicator remain available. */
	OPENBGL
}

static void _e2_tray_notification_bus_ready (GObject *source,
	GAsyncResult *result, gpointer data)
{
	CLOSEBGL
	GError *error = NULL;
	GDBusConnection *bus = g_bus_get_finish (result, &error);
	g_clear_error (&error);
	if (bus == NULL)
	{
		OPENBGL
		return;
	}
	if (GPOINTER_TO_UINT (data) != notification_generation)
	{
		g_object_unref (bus);
		OPENBGL
		return;
	}
	notification_bus = bus;
	action_subscription = g_dbus_connection_signal_subscribe (bus,
		"org.freedesktop.Notifications", "org.freedesktop.Notifications",
		"ActionInvoked", "/org/freedesktop/Notifications", NULL,
		G_DBUS_SIGNAL_FLAGS_NONE, _e2_tray_notification_action, NULL, NULL);
	GVariantBuilder hints;
	g_variant_builder_init (&hints, G_VARIANT_TYPE ("a{sv}"));
	g_variant_builder_add (&hints, "{sv}", "suppress-sound", g_variant_new_boolean (TRUE));
	g_variant_builder_add (&hints, "{sv}", "urgency", g_variant_new_byte (0));
	/* Some notification servers render the default action as a button too.
	   Advertise just one explicit Review action to avoid duplicate buttons. */
	const gchar *actions[] = { "review", _("Review"), NULL };
	gchar *summary = g_strdup_printf (_("%s: needs attention (%u)"), PROGNAME, notification_count);
	gchar *icon = e2_icons_get_application_path ();
	g_dbus_connection_call (bus, "org.freedesktop.Notifications",
		"/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify",
		g_variant_new ("(susss^asa{sv}i)", PROGNAME, 0, icon != NULL ? icon : BINNAME,
			summary, _("An operation is waiting for your answer. Review it from the tray menu or the main window."),
			actions, &hints, -1), G_VARIANT_TYPE ("(u)"),
		G_DBUS_CALL_FLAGS_NONE, 2000, NULL, _e2_tray_notified, data);
	g_free (summary);
	g_free (icon);
	OPENBGL
}

static gboolean _e2_tray_send_notification (gpointer unused)
{
	CLOSEBGL
	notification_timer = 0;
	g_bus_get (G_BUS_TYPE_SESSION, NULL, _e2_tray_notification_bus_ready,
		GUINT_TO_POINTER (notification_generation));
	OPENBGL
	return FALSE;
}
#endif

void e2_tray_notify_pending (guint count)
{
#if GLIB_CHECK_VERSION(2,26,0)
	gboolean enabled = e2_tray_is_active () && count != 0
		&& e2_option_bool_get ("tray-notifications");
	notification_count = count;
	if (!enabled)
	{
		notification_generation++;
		if (notification_timer != 0)
			g_source_remove (notification_timer);
		notification_timer = 0;
		if (notification_bus != NULL)
		{
			_e2_tray_close_notification (notification_bus, notification_id);
			g_dbus_connection_signal_unsubscribe (notification_bus, action_subscription);
			g_object_unref (notification_bus);
			notification_bus = NULL;
		}
		notification_id = 0;
		notification_batch = FALSE;
	}
	else if (!notification_batch)
	{
		/* One notification per uninterrupted batch, coalescing arrivals.
		   Dismissal is never a dialog response and never triggers a reminder. */
		notification_batch = TRUE;
		notification_timer = g_timeout_add (200, _e2_tray_send_notification, NULL);
	}
#endif
}
