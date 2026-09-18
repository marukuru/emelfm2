/* Application windows and deferred questions. GPL version 3 or later. */
#include "emelfm2.h"
#include "e2_tray.h"
#if defined(USE_GTK3_0) && defined(GDK_WINDOWING_X11)
#include <gtk/gtkx.h>
#endif

typedef struct
{
	GtkWidget *window;
	gboolean restore, question, pending, reviewing, show_all;
	gboolean old_skip, managed, added_parent, transfer;
} E2_TrayWindow;

static GList *windows;
static guint update_source;
static GtkWidget *banner, *banner_label;
static GtkWidget *questions_item, *transfers_item;
static gboolean managing;

static gboolean _e2_tray_visible (GtkWidget *widget)
{
#ifdef USE_GTK2_18
	return gtk_widget_get_visible (widget);
#else
	return GTK_WIDGET_VISIBLE (widget);
#endif
}

static E2_TrayWindow *_e2_tray_window (GtkWidget *window)
{
	return g_object_get_data (G_OBJECT (window), "e2-tray-window");
}

static void _e2_tray_schedule (void);

static void _e2_tray_manage (E2_TrayWindow *record, gboolean enabled)
{
	GtkWindow *window = GTK_WINDOW (record->window);
	if (enabled && !record->managed)
	{
		record->old_skip = gtk_window_get_skip_taskbar_hint (window);
		record->added_parent = gtk_window_get_transient_for (window) == NULL;
		if (record->added_parent)
			gtk_window_set_transient_for (window, GTK_WINDOW (app.main_window));
		gtk_window_set_skip_taskbar_hint (window, TRUE);
		record->managed = TRUE;
	}
	else if (!enabled && record->managed)
	{
		gtk_window_set_skip_taskbar_hint (window, record->old_skip);
		if (record->added_parent)
			gtk_window_set_transient_for (window, NULL);
		record->managed = FALSE;
	}
}

static void _e2_tray_destroy_cb (GtkWidget *window, gpointer unused)
{
	E2_TrayWindow *record = _e2_tray_window (window);
	if (record == NULL)
		return;
	windows = g_list_remove (windows, record);
	g_object_set_data (G_OBJECT (window), "e2-tray-window", NULL);
	g_free (record);
	_e2_tray_schedule ();
}

static void _e2_tray_response_cb (GtkDialog *window, gint response, gpointer unused)
{
	E2_TrayWindow *record = _e2_tray_window (GTK_WIDGET (window));
	if (record != NULL)
	{
		if (record->reviewing)
			gtk_window_set_modal (GTK_WINDOW (window), FALSE);
		record->pending = record->reviewing = record->question = FALSE;
		record->restore = FALSE;
		_e2_tray_schedule ();
	}
}

void e2_tray_register_window (GtkWidget *window)
{
	if (window == app.main_window || !GTK_IS_WINDOW (window)
		|| _e2_tray_window (window) != NULL)
		return;
#ifdef GDK_WINDOWING_X11
	/* GTK's toplevel list also contains GtkPlug windows, including the
	   GtkStatusIcon's own GtkTrayIcon. The desktop owns their visibility:
	   treating one as a dialog hides the tray icon along with the app. */
	if (GTK_IS_PLUG (window))
		return;
#endif
	E2_TrayWindow *record = g_new0 (E2_TrayWindow, 1);
	record->window = window;
	windows = g_list_append (windows, record);
	g_object_set_data (G_OBJECT (window), "e2-tray-window", record);
	g_signal_connect (window, "destroy", G_CALLBACK (_e2_tray_destroy_cb), NULL);
	if (GTK_IS_DIALOG (window))
		g_signal_connect (window, "response", G_CALLBACK (_e2_tray_response_cb), NULL);
	_e2_tray_manage (record, managing);
}

/* Called before showing a dialog or making it modal. A queued question keeps
   its existing operation wait loop, but has neither a GTK grab nor a map. */
gboolean e2_tray_defer_dialog (GtkWidget *window, gboolean question,
	gboolean show_all)
{
	e2_tray_register_window (window);
	E2_TrayWindow *record = _e2_tray_window (window);
	record->question = question;
	record->show_all = show_all;
	gboolean other_pending = FALSE;
	GList *iter;
	for (iter = windows; question && iter != NULL; iter = iter->next)
		other_pending |= ((E2_TrayWindow*)iter->data)->pending;
	/* A review opens only its own parent chain. Other background windows
	   stay hidden until the user explicitly restores the whole application. */
	gboolean background = !question && record->restore && !_e2_tray_visible (window);
	if (record->pending || (e2_tray_is_active ()
		&& (e2_tray_is_hidden () || other_pending || background)))
	{
		if (question)
		{
			record->pending = TRUE;
			gtk_window_set_modal (GTK_WINDOW (window), FALSE);
		}
		else
			record->restore = TRUE;
		_e2_tray_schedule ();
		return TRUE;
	}
	return FALSE;
}

void e2_tray_wait_dialog (GtkWidget *window)
{
	e2_tray_register_window (window);
	e2_tray_defer_dialog (window, TRUE, _e2_tray_window (window)->show_all);
}

void e2_tray_register_transfer (GtkWidget *window)
{
	e2_tray_register_window (window);
	_e2_tray_window (window)->transfer = TRUE;
	_e2_tray_schedule ();
}

static void _e2_tray_scan_windows (void)
{
	GList *list = gtk_window_list_toplevels (), *iter;
	for (iter = list; iter != NULL; iter = iter->next)
	{
		GtkWindowType type;
		g_object_get (iter->data, "type", &type, NULL);
		if (type == GTK_WINDOW_TOPLEVEL)
			e2_tray_register_window (iter->data);
	}
	g_list_free (list);
}

void e2_tray_windows_hide (void)
{
	_e2_tray_scan_windows ();
	GList *iter;
	for (iter = windows; iter != NULL; iter = iter->next)
	{
		E2_TrayWindow *record = iter->data;
		if (_e2_tray_visible (record->window))
		{
			if (record->question || gtk_window_get_modal (GTK_WINDOW (record->window)))
			{
				record->question = record->pending = TRUE;
				record->reviewing = FALSE;
				gtk_window_set_modal (GTK_WINDOW (record->window), FALSE);
			}
			else
				record->restore = TRUE;
			gtk_widget_hide (record->window);
		}
	}
	_e2_tray_schedule ();
}

void e2_tray_windows_restore (void)
{
	GList *iter;
	for (iter = windows; iter != NULL; iter = iter->next)
	{
		E2_TrayWindow *record = iter->data;
		if (record->restore && !record->pending)
		{
			record->restore = FALSE;
			if (record->show_all)
				gtk_widget_show_all (record->window);
			else
				gtk_widget_show (record->window);
		}
	}
	_e2_tray_schedule ();
}

static void _e2_tray_review_cb (GtkWidget *item, GtkWidget *window)
{
	E2_TrayWindow *record = _e2_tray_window (window);
	if (record == NULL)
		return;
	/* Review one question at a time, without automatically opening the next. */
	GList *iter;
	for (iter = windows; iter != NULL; iter = iter->next)
	{
		E2_TrayWindow *other = iter->data;
		if (other->reviewing)
		{
			gtk_window_present (GTK_WINDOW (other->window));
			return;
		}
	}
	e2_tray_show_for_review ();
	GtkWindow *parent = gtk_window_get_transient_for (GTK_WINDOW (window));
	if (parent != NULL && !_e2_tray_visible (GTK_WIDGET (parent)))
		gtk_widget_show (GTK_WIDGET (parent));
	if (record->pending)
	{
		record->reviewing = TRUE;
		gtk_window_set_modal (GTK_WINDOW (window), TRUE);
	}
	record->restore = FALSE;
	if (record->show_all)
		gtk_widget_show_all (window);
	else
		gtk_widget_show (window);
	gtk_window_present (GTK_WINDOW (window));
}

void e2_tray_review_pending (void)
{
	GList *iter;
	for (iter = windows; iter != NULL; iter = iter->next)
	{
		E2_TrayWindow *record = iter->data;
		if (record->pending)
		{
			_e2_tray_review_cb (NULL, record->window);
			return;
		}
	}
}

static void _e2_tray_review_first_cb (GtkWidget *button, gpointer unused)
{
	e2_tray_review_pending ();
}

static void _e2_tray_fill_menu (GtkWidget *item, gboolean questions)
{
	GtkWidget *submenu = gtk_menu_new ();
	GList *iter;
	for (iter = windows; iter != NULL; iter = iter->next)
	{
		E2_TrayWindow *record = iter->data;
		if (questions ? !record->pending : !record->transfer)
			continue;
		const gchar *label = gtk_window_get_title (GTK_WINDOW (record->window));
		GtkWidget *child = gtk_menu_item_new_with_label (label != NULL ? label : PROGNAME);
		gtk_menu_shell_append (GTK_MENU_SHELL (submenu), child);
		g_signal_connect_object (child, "activate", G_CALLBACK (_e2_tray_review_cb),
			record->window, 0);
	}
	GtkWidget *old = gtk_menu_item_get_submenu (GTK_MENU_ITEM (item));
	if (old != NULL)
		gtk_widget_destroy (old);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
	gtk_widget_show_all (submenu);
}

static gboolean _e2_tray_update_windows (gpointer unused)
{
	CLOSEBGL
	update_source = 0;
	guint count = 0, transfers = 0;
	GList *iter;
	for (iter = windows; iter != NULL; iter = iter->next)
	{
		E2_TrayWindow *record = iter->data;
		count += record->pending;
		transfers += record->transfer;
	}
	gchar *label = g_strdup_printf (_("Needs attention (%u)"), count);
	if (questions_item != NULL)
	{
		gtk_label_set_text (GTK_LABEL (gtk_bin_get_child (GTK_BIN (questions_item))), label);
		_e2_tray_fill_menu (questions_item, TRUE);
		if (count) gtk_widget_show (questions_item);
		else gtk_widget_hide (questions_item);
	}
	if (transfers_item != NULL)
	{
		gchar *title = g_strdup_printf (_("Transfers (%u)"), transfers);
		gtk_label_set_text (GTK_LABEL (gtk_bin_get_child (GTK_BIN (transfers_item))), title);
		g_free (title);
		_e2_tray_fill_menu (transfers_item, FALSE);
		if (transfers) gtk_widget_show (transfers_item);
		else gtk_widget_hide (transfers_item);
	}
	if (count && banner == NULL && app.vbox_main != NULL)
	{
#ifdef USE_GTK3_0
		banner = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
#else
		banner = gtk_hbox_new (FALSE, 6);
#endif
		g_object_add_weak_pointer (G_OBJECT (banner), (gpointer*)&banner);
		banner_label = gtk_label_new (NULL);
		GtkWidget *button = gtk_button_new_with_mnemonic (_("_Review"));
		g_signal_connect (button, "clicked", G_CALLBACK (_e2_tray_review_first_cb), NULL);
		gtk_box_pack_start (GTK_BOX (banner), banner_label, TRUE, TRUE, 6);
		gtk_box_pack_end (GTK_BOX (banner), button, FALSE, FALSE, 6);
		gtk_box_pack_start (GTK_BOX (app.vbox_main), banner, FALSE, FALSE, 0);
		gtk_box_reorder_child (GTK_BOX (app.vbox_main), banner, 0);
		gtk_widget_show_all (banner);
		gtk_widget_set_no_show_all (banner, TRUE);
	}
	if (banner != NULL)
	{
		gtk_label_set_text (GTK_LABEL (banner_label), label);
		if (count)
		{
			gtk_widget_show_all (banner);
			gtk_widget_show (banner);
		}
		else gtk_widget_hide (banner);
	}
	g_free (label);
	e2_tray_set_attention (count);
	e2_tray_notify_pending (count);
	OPENBGL
	return FALSE;
}

static void _e2_tray_schedule (void)
{
	if (update_source == 0)
		update_source = g_idle_add (_e2_tray_update_windows, NULL);
}

void e2_tray_windows_menu (GtkWidget *menu)
{
	questions_item = gtk_menu_item_new_with_label ("");
	transfers_item = gtk_menu_item_new_with_label ("");
	g_object_add_weak_pointer (G_OBJECT (questions_item), (gpointer*)&questions_item);
	g_object_add_weak_pointer (G_OBJECT (transfers_item), (gpointer*)&transfers_item);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), questions_item);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), transfers_item);
	_e2_tray_schedule ();
}

void e2_tray_windows_sync (gboolean enabled)
{
	managing = enabled;
	_e2_tray_scan_windows ();
	GList *iter;
	for (iter = windows; iter != NULL; iter = iter->next)
		_e2_tray_manage (iter->data, enabled);
	_e2_tray_schedule ();
}

void e2_tray_windows_cleanup (void)
{
	if (update_source != 0)
		g_source_remove (update_source);
	update_source = 0;
	while (windows != NULL)
	{
		E2_TrayWindow *record = windows->data;
		g_signal_handlers_disconnect_by_func (record->window, _e2_tray_destroy_cb, NULL);
		g_signal_handlers_disconnect_by_func (record->window, _e2_tray_response_cb, NULL);
		g_object_set_data (G_OBJECT (record->window), "e2-tray-window", NULL);
		windows = g_list_delete_link (windows, windows);
		g_free (record);
	}
	if (banner != NULL)
		gtk_widget_destroy (banner);
	managing = FALSE;
	e2_tray_notify_pending (0);
}
