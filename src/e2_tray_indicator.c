/* StatusNotifierItem and D-Bus menu export. GPL version 3 or later.
   Keep the transport independent of the panel's GTK version. */
#include "e2_tray_indicator.h"
#include "e2_output.h"
#if GLIB_CHECK_VERSION(2,26,0) && defined(USE_GTK2_10)
#include <gio/gio.h>
#include <gmodule.h>
#include <unistd.h>

#define SNI_INTERFACE "org.kde.StatusNotifierItem"
#define SNI_WATCHER "org.kde.StatusNotifierWatcher"

/* libdbusmenu-glib has no GTK dependency. Both legacy and Ayatana indicator
   services, XFCE Status Tray and GNOME indicator extensions use this protocol. */
static GModule *menu_module;
static GObject *(*menu_server_new) (const gchar *);
static void (*menu_server_set_root) (GObject *, GObject *);
static GObject *(*menu_item_new) (void);
static gboolean (*menu_item_append) (GObject *, GObject *);
static gboolean (*menu_item_set) (GObject *, const gchar *, const gchar *);
static gboolean (*menu_item_set_bool) (GObject *, const gchar *, gboolean);

gboolean e2_tray_indicator_available (void)
{
	if (menu_module != NULL) return TRUE;
	GModule *module = g_module_open ("libdbusmenu-glib.so.4", G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
	if (module == NULL) return FALSE;
	if (!g_module_symbol (module, "dbusmenu_server_new", (gpointer*)&menu_server_new)
		|| !g_module_symbol (module, "dbusmenu_server_set_root", (gpointer*)&menu_server_set_root)
		|| !g_module_symbol (module, "dbusmenu_menuitem_new", (gpointer*)&menu_item_new)
		|| !g_module_symbol (module, "dbusmenu_menuitem_child_append", (gpointer*)&menu_item_append)
		|| !g_module_symbol (module, "dbusmenu_menuitem_property_set", (gpointer*)&menu_item_set)
		|| !g_module_symbol (module, "dbusmenu_menuitem_property_set_bool", (gpointer*)&menu_item_set_bool))
	{
		g_module_close (module);
		return FALSE;
	}
	g_module_make_resident (module);
	menu_module = module;
	return TRUE;
}

typedef struct
{
	GObject parent;
	GDBusConnection *bus;
	GCancellable *cancel;
	GObject *server;
	GtkMenu *menu;
	gchar *id, *path, *menu_path, *bus_name, *icon, *attention_icon, *label, *guide;
	guint registration, watch, owner, generation;
	gint status, menu_x, menu_y;
	gboolean connected, stopped;
} E2TrayIndicator;
typedef GObjectClass E2TrayIndicatorClass;
G_DEFINE_TYPE (E2TrayIndicator, e2_tray_indicator, G_TYPE_OBJECT)

static GDBusNodeInfo *item_info;
static guint connection_signal, item_serial;
static const gchar item_xml[] =
	"<node><interface name='" SNI_INTERFACE "'>"
	"<property name='Id' type='s' access='read'/>"
	"<property name='Category' type='s' access='read'/>"
	"<property name='Title' type='s' access='read'/>"
	"<property name='Status' type='s' access='read'/>"
	"<property name='WindowId' type='u' access='read'/>"
	"<property name='IconName' type='s' access='read'/>"
	"<property name='IconPixmap' type='a(iiay)' access='read'/>"
	"<property name='IconThemePath' type='s' access='read'/>"
	"<property name='IconAccessibleDesc' type='s' access='read'/>"
	"<property name='AttentionIconName' type='s' access='read'/>"
	"<property name='AttentionIconPixmap' type='a(iiay)' access='read'/>"
	"<property name='AttentionMovieName' type='s' access='read'/>"
	"<property name='AttentionAccessibleDesc' type='s' access='read'/>"
	"<property name='OverlayIconName' type='s' access='read'/>"
	"<property name='OverlayIconPixmap' type='a(iiay)' access='read'/>"
	"<property name='ToolTip' type='(sa(iiay)ss)' access='read'/>"
	"<property name='ItemIsMenu' type='b' access='read'/>"
	"<property name='Menu' type='o' access='read'/>"
	"<property name='XAyatanaLabel' type='s' access='read'/>"
	"<property name='XAyatanaLabelGuide' type='s' access='read'/>"
	"<property name='XAyatanaOrderingIndex' type='u' access='read'/>"
	"<method name='ContextMenu'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
	"<method name='Activate'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
	"<method name='SecondaryActivate'><arg type='i' direction='in'/><arg type='i' direction='in'/></method>"
	"<method name='XAyatanaSecondaryActivate'><arg type='u' direction='in'/></method>"
	"<method name='Scroll'><arg type='i' direction='in'/><arg type='s' direction='in'/></method>"
	"<signal name='NewIcon'/><signal name='NewAttentionIcon'/><signal name='NewToolTip'/>"
	"<signal name='NewStatus'><arg type='s'/></signal>"
	"<signal name='XAyatanaNewLabel'><arg type='s'/><arg type='s'/></signal>"
	"</interface></node>";

static const gchar *_e2_indicator_status (E2TrayIndicator *self)
{
	return self->status == 0 ? "Passive" : self->status == 2 ? "NeedsAttention" : "Active";
}

static GVariant *_e2_indicator_property (GDBusConnection *bus, const gchar *sender,
	const gchar *path, const gchar *interface, const gchar *name, GError **error, gpointer data)
{
	CLOSEBGL
	E2TrayIndicator *self = data;
	GVariant *value;
	const gchar *text = "";
	if (!strcmp (name, "Menu")) value = g_variant_new_object_path (self->menu_path);
	else if (!strcmp (name, "ItemIsMenu")) value = g_variant_new_boolean (TRUE);
	else if (!strcmp (name, "WindowId") || !strcmp (name, "XAyatanaOrderingIndex"))
		value = g_variant_new_uint32 (0);
	else if (g_str_has_suffix (name, "Pixmap"))
		value = g_variant_new_array (G_VARIANT_TYPE ("(iiay)"), NULL, 0);
	else if (!strcmp (name, "ToolTip"))
		value = g_variant_new ("(s@a(iiay)ss)", self->icon,
			g_variant_new_array (G_VARIANT_TYPE ("(iiay)"), NULL, 0), PROGNAME, self->label);
	else
	{
		if (!strcmp (name, "Id")) text = self->id;
		else if (!strcmp (name, "Category")) text = "ApplicationStatus";
		else if (!strcmp (name, "Title") || !strcmp (name, "IconAccessibleDesc")) text = PROGNAME;
		else if (!strcmp (name, "Status")) text = _e2_indicator_status (self);
		else if (!strcmp (name, "IconName")) text = self->icon;
		else if (!strcmp (name, "AttentionIconName")) text = self->attention_icon;
		else if (!strcmp (name, "XAyatanaLabel") || !strcmp (name, "AttentionAccessibleDesc")) text = self->label;
		else if (!strcmp (name, "XAyatanaLabelGuide")) text = self->guide;
		value = g_variant_new_string (text);
	}
	OPENBGL
	return value;
}

static void _e2_indicator_menu_position (GtkMenu *menu, gint *x, gint *y,
	gboolean *push_in, gpointer data)
{
	E2TrayIndicator *self = data;
	*x = self->menu_x;
	*y = self->menu_y;
	*push_in = TRUE;
}

static void _e2_indicator_method (GDBusConnection *bus, const gchar *sender,
	const gchar *path, const gchar *interface, const gchar *method,
	GVariant *parameters, GDBusMethodInvocation *invocation, gpointer data)
{
	CLOSEBGL
	E2TrayIndicator *self = data;
	/* ItemIsMenu lets the host render the exported menu. Hosts which instead
	   request activation explicitly still get a menu, never a window toggle. */
	if ((!strcmp (method, "ContextMenu") || !strcmp (method, "Activate")) && self->menu != NULL)
	{
		g_variant_get (parameters, "(ii)", &self->menu_x, &self->menu_y);
		gtk_menu_popup (self->menu, NULL, NULL, _e2_indicator_menu_position,
			self, 0, gtk_get_current_event_time ());
	}
	g_dbus_method_invocation_return_value (invocation, NULL);
	OPENBGL
}

static void _e2_indicator_emit (E2TrayIndicator *self, const gchar *signal, GVariant *parameters)
{
	if (self->bus != NULL && !self->stopped)
		g_dbus_connection_emit_signal (self->bus, NULL, self->path,
			SNI_INTERFACE, signal, parameters, NULL);
	else if (parameters != NULL)
		g_variant_unref (g_variant_ref_sink (parameters));
}

static void _e2_indicator_connected (E2TrayIndicator *self, gboolean connected)
{
	if (self->connected != connected && !self->stopped)
	{
		self->connected = connected;
		g_signal_emit (self, connection_signal, 0, connected);
	}
}

typedef struct { E2TrayIndicator *self; guint generation; } E2IndicatorRequest;

static void _e2_indicator_registered (GObject *bus, GAsyncResult *result, gpointer data)
{
	CLOSEBGL
	E2IndicatorRequest *request = data;
	GVariant *reply = g_dbus_connection_call_finish (G_DBUS_CONNECTION (bus), result, NULL);
	if (request->generation == request->self->generation)
		_e2_indicator_connected (request->self, reply != NULL);
	if (reply != NULL) g_variant_unref (reply);
	g_object_unref (request->self);
	g_free (request);
	OPENBGL
}

static void _e2_indicator_watcher_appeared (GDBusConnection *bus, const gchar *name,
	const gchar *owner, gpointer data)
{
	CLOSEBGL
	E2TrayIndicator *self = data;
	E2IndicatorRequest *request = g_new (E2IndicatorRequest, 1);
	request->self = g_object_ref (self);
	request->generation = ++self->generation;
	/* A name owned only for this indicator lets the watcher remove the item
	   on disable, even while emelFM2's shared session connection stays open. */
	g_dbus_connection_call (bus, owner, "/StatusNotifierWatcher", SNI_WATCHER,
		"RegisterStatusNotifierItem", g_variant_new ("(s)", self->bus_name), NULL,
		G_DBUS_CALL_FLAGS_NONE, 3000, self->cancel, _e2_indicator_registered, request);
	OPENBGL
}

static void _e2_indicator_watcher_vanished (GDBusConnection *bus, const gchar *name, gpointer data)
{
	CLOSEBGL
	E2TrayIndicator *self = data;
	self->generation++;
	_e2_indicator_connected (self, FALSE);
	OPENBGL
}

static void _e2_indicator_name_acquired (GDBusConnection *bus, const gchar *name, gpointer data)
{
	CLOSEBGL
	E2TrayIndicator *self = data;
	if (!self->stopped && self->watch == 0)
		self->watch = g_bus_watch_name_on_connection (bus, SNI_WATCHER,
			G_BUS_NAME_WATCHER_FLAGS_NONE, _e2_indicator_watcher_appeared,
			_e2_indicator_watcher_vanished, self, NULL);
	OPENBGL
}

static void _e2_indicator_name_lost (GDBusConnection *bus, const gchar *name, gpointer data)
{
	CLOSEBGL
	E2TrayIndicator *self = data;
	if (self->watch != 0) g_bus_unwatch_name (self->watch);
	self->watch = 0;
	self->generation++;
	_e2_indicator_connected (self, FALSE);
	OPENBGL
}

static void _e2_indicator_bus_ready (GObject *source, GAsyncResult *result, gpointer data)
{
	CLOSEBGL
	E2TrayIndicator *self = data;
	GDBusConnection *bus = g_bus_get_finish (result, NULL);
	if (bus != NULL && !self->stopped)
	{
		static const GDBusInterfaceVTable vtable = { _e2_indicator_method, _e2_indicator_property, NULL };
		self->bus = g_object_ref (bus);
		self->registration = g_dbus_connection_register_object (bus, self->path,
			item_info->interfaces[0], &vtable, self, NULL, NULL);
		if (self->registration != 0)
			self->owner = g_bus_own_name_on_connection (bus, self->bus_name,
				G_BUS_NAME_OWNER_FLAGS_NONE, _e2_indicator_name_acquired,
				_e2_indicator_name_lost, self, NULL);
	}
	if (bus != NULL) g_object_unref (bus);
	if (!self->stopped && self->registration == 0)
		e2_output_print_error (_("Could not connect the tray icon to the desktop session bus."), FALSE);
	g_object_unref (self);
	OPENBGL
}

void e2_tray_indicator_stop (GObject *object)
{
	E2TrayIndicator *self = (E2TrayIndicator*)object;
	self->stopped = TRUE;
	g_cancellable_cancel (self->cancel);
	if (self->watch != 0) g_bus_unwatch_name (self->watch);
	self->watch = 0;
	if (self->owner != 0) g_bus_unown_name (self->owner);
	self->owner = 0;
	if (self->registration != 0) g_dbus_connection_unregister_object (self->bus, self->registration);
	self->registration = 0;
	if (self->server != NULL) g_object_unref (self->server);
	self->server = NULL;
	if (self->menu != NULL) g_object_unref (self->menu);
	self->menu = NULL;
}

static void _e2_indicator_dispose (GObject *object)
{
	e2_tray_indicator_stop (object);
	G_OBJECT_CLASS (e2_tray_indicator_parent_class)->dispose (object);
}

static void _e2_indicator_finalize (GObject *object)
{
	E2TrayIndicator *self = (E2TrayIndicator*)object;
	if (self->bus != NULL) g_object_unref (self->bus);
	g_object_unref (self->cancel);
	g_free (self->id);
	g_free (self->path);
	g_free (self->menu_path);
	g_free (self->bus_name);
	g_free (self->icon);
	g_free (self->attention_icon);
	g_free (self->label);
	g_free (self->guide);
	G_OBJECT_CLASS (e2_tray_indicator_parent_class)->finalize (object);
}

static void e2_tray_indicator_class_init (E2TrayIndicatorClass *klass)
{
	klass->dispose = _e2_indicator_dispose;
	klass->finalize = _e2_indicator_finalize;
	connection_signal = g_signal_new ("connection-changed", G_TYPE_FROM_CLASS (klass),
		G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_VOID__BOOLEAN,
		G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
	item_info = g_dbus_node_info_new_for_xml (item_xml, NULL);
}

static void e2_tray_indicator_init (E2TrayIndicator *self)
{
	self->cancel = g_cancellable_new ();
	self->label = g_strdup ("");
	self->guide = g_strdup ("");
}

GObject *e2_tray_indicator_new (const gchar *id, const gchar *icon, gint category)
{
	E2TrayIndicator *self = g_object_new (e2_tray_indicator_get_type (), NULL);
	self->id = g_strdup (id);
	self->icon = g_strdup (icon);
	self->attention_icon = g_strdup (icon);
	self->path = g_strdup ("/StatusNotifierItem");
	self->bus_name = g_strdup_printf ("org.kde.StatusNotifierItem-%u-%u", (guint)getpid (), ++item_serial);
	self->menu_path = g_strdup_printf ("/org/emelfm2/TrayMenu/%u", item_serial);
	self->server = menu_server_new (self->menu_path);
	g_bus_get (G_BUS_TYPE_SESSION, self->cancel, _e2_indicator_bus_ready, g_object_ref (self));
	return G_OBJECT (self);
}

static void _e2_indicator_menu_activate (GObject *item, guint timestamp, GtkWidget *widget)
{
	CLOSEBGL
	gboolean visible, sensitive;
	g_object_get (widget, "visible", &visible, "sensitive", &sensitive, NULL);
	if (sensitive && visible)
		gtk_menu_item_activate (GTK_MENU_ITEM (widget));
	OPENBGL
}

static void _e2_indicator_menu_children (GObject *parent, GtkMenu *menu)
{
	GList *children = gtk_container_get_children (GTK_CONTAINER (menu)), *iter;
	for (iter = children; iter != NULL; iter = iter->next)
	{
		GtkWidget *widget = iter->data;
		GObject *item = menu_item_new ();
		gboolean visible, sensitive;
		g_object_get (widget, "visible", &visible, "sensitive", &sensitive, NULL);
		menu_item_set_bool (item, "visible", visible);
		menu_item_set_bool (item, "enabled", sensitive);
		if (GTK_IS_SEPARATOR_MENU_ITEM (widget)) menu_item_set (item, "type", "separator");
		else
		{
			GtkWidget *label = gtk_bin_get_child (GTK_BIN (widget));
			const gchar *text = GTK_IS_LABEL (label) ? gtk_label_get_label (GTK_LABEL (label)) : "";
			/* DBusMenu labels use underscores for mnemonics. Preserve literal
			   underscores in question/window titles created without mnemonics. */
			GString *escaped = g_string_new (NULL);
			gboolean mnemonic = GTK_IS_LABEL (label) && gtk_label_get_use_underline (GTK_LABEL (label));
			for (; *text != '\0'; text++)
			{
				if (*text == '_' && !mnemonic) g_string_append_c (escaped, '_');
				g_string_append_c (escaped, *text);
			}
			menu_item_set (item, "label", escaped->str);
			g_string_free (escaped, TRUE);
			GtkWidget *submenu = gtk_menu_item_get_submenu (GTK_MENU_ITEM (widget));
			if (submenu != NULL)
			{
				menu_item_set (item, "children-display", "submenu");
				_e2_indicator_menu_children (item, GTK_MENU (submenu));
			}
			else g_signal_connect_object (item, "item-activated",
				G_CALLBACK (_e2_indicator_menu_activate), widget, 0);
		}
		menu_item_append (parent, item);
		g_object_unref (item);
	}
	g_list_free (children);
}

void e2_tray_indicator_set_menu (GObject *object, GtkMenu *menu)
{
	E2TrayIndicator *self = (E2TrayIndicator*)object;
	if (menu != self->menu)
	{
		if (self->menu != NULL) g_object_unref (self->menu);
		self->menu = g_object_ref (menu);
	}
	GObject *root = menu_item_new ();
	_e2_indicator_menu_children (root, menu);
	menu_server_set_root (self->server, root);
	g_object_unref (root);
}

void e2_tray_indicator_set_status (GObject *object, gint status)
{
	E2TrayIndicator *self = (E2TrayIndicator*)object;
	if (self->status == status) return;
	self->status = status;
	_e2_indicator_emit (self, "NewStatus", g_variant_new ("(s)", _e2_indicator_status (self)));
}

void e2_tray_indicator_set_icon (GObject *object, const gchar *icon)
{
	E2TrayIndicator *self = (E2TrayIndicator*)object;
	if (!g_strcmp0 (self->icon, icon)) return;
	g_free (self->icon);
	self->icon = g_strdup (icon);
	_e2_indicator_emit (self, "NewIcon", NULL);
}

void e2_tray_indicator_set_attention_icon (GObject *object, const gchar *icon)
{
	E2TrayIndicator *self = (E2TrayIndicator*)object;
	if (!g_strcmp0 (self->attention_icon, icon)) return;
	g_free (self->attention_icon);
	self->attention_icon = g_strdup (icon);
	_e2_indicator_emit (self, "NewAttentionIcon", NULL);
}

void e2_tray_indicator_set_label (GObject *object, const gchar *label, const gchar *guide)
{
	E2TrayIndicator *self = (E2TrayIndicator*)object;
	if (!g_strcmp0 (self->label, label) && !g_strcmp0 (self->guide, guide)) return;
	g_free (self->label);
	g_free (self->guide);
	self->label = g_strdup (label);
	self->guide = g_strdup (guide);
	_e2_indicator_emit (self, "XAyatanaNewLabel", g_variant_new ("(ss)", label, guide));
	_e2_indicator_emit (self, "NewToolTip", NULL);
}
#endif
