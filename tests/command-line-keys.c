/* Exercise command-line keys through the real window dispatch path. */
#include "emelfm2.h"
#include "e2_action.h"
#include "e2_command_line.h"
#include "e2_combobox.h"
#include "e2_keybinding.h"
#include <gdk/gdkkeysyms.h>

static GtkWidget *entry;

static void key (guint keyval, GdkModifierType state)
{
	/* Exercise GTK's grab routing as well as the application's window handler. */
	GdkEvent *event = gdk_event_new (GDK_KEY_PRESS);
	event->key.keyval = keyval;
	event->key.state = state;
	GtkWidget *focus = gtk_window_get_focus (GTK_WINDOW (app.main_window));
	event->key.window = g_object_ref (gtk_widget_get_window (focus));
	GdkKeymapKey *keys;
	gint count;
	if (gdk_keymap_get_entries_for_keyval (gdk_keymap_get_default (), keyval, &keys, &count))
	{
		event->key.hardware_keycode = keys[0].keycode;
		event->key.group = keys[0].group;
		g_free (keys);
	}
#ifdef USE_GTK3_0
	GdkDevice *pointer = gdk_device_manager_get_client_pointer (
		gdk_display_get_device_manager (gdk_display_get_default ()));
	gdk_event_set_device (event, gdk_device_get_associated_device (pointer));
#endif
	gtk_main_do_event (event);
	gdk_event_free (event);
}

static void assert_entry (const gchar *text)
{
	g_assert_true (gtk_window_get_focus (GTK_WINDOW (app.main_window)) == entry);
	g_assert_cmpstr (gtk_entry_get_text (GTK_ENTRY (entry)), ==, text);
}

static void set_text (const gchar *text)
{
	gtk_widget_grab_focus (entry);
	gtk_entry_set_text (GTK_ENTRY (entry), text);
	gtk_editable_set_position (GTK_EDITABLE (entry), -1);
}

static void complete (const gchar *text, const gchar *expected)
{
	static guint check;
	g_print ("completion check %u: %s\n", ++check, text);
	set_text (text);
	key (GDK_Tab, 0);
	assert_entry (expected);
}

static gboolean tick (gpointer data)
{
	if (app.main_window == NULL || curr_view == NULL || !app.pane1.view.dir[0]
		|| !app.pane2.view.dir[0] || g_atomic_int_get (&app.pane1.view.listcontrols.cd_working)
		|| g_atomic_int_get (&app.pane2.view.listcontrols.cd_working)) return TRUE;
	CLOSEBGL
	E2_CommandLineRuntime *command = NULL, *directory = NULL;
	for (GList *p = app.command_lines; p != NULL; p = p->next)
	{
		E2_CommandLineRuntime *rt = p->data;
		if (rt->original) command = rt;
		else directory = rt;
	}
	g_assert_nonnull (command);
	g_assert_nonnull (directory);
	entry = gtk_bin_get_child (GTK_BIN (command->combo));
	/* Xvfb has no window manager to deliver a focus-in event. */
	GdkEvent *focus = gdk_event_new (GDK_FOCUS_CHANGE);
	focus->focus_change.window = g_object_ref (gtk_widget_get_window (app.main_window));
	focus->focus_change.in = TRUE;
	gtk_widget_event (app.main_window, focus);
	gdk_event_free (focus);

	complete ("e2-completion-pro", "e2-completion-probe ");
	complete ("cat unique-fi", "cat unique-file.txt ");
	complete ("cat folder-na", "cat folder-name/");
	complete ("cat spaced", "cat spaced\\ file.txt ");
	complete ("cat no-such-completion", "cat no-such-completion");
	complete ("", "");
	set_text ("cat unique-fi --flag");
	gtk_editable_set_position (GTK_EDITABLE (entry), 13);
	key (GDK_Tab, GDK_LOCK_MASK);
	assert_entry ("cat unique-file.txt  --flag");
	g_assert_cmpint (gtk_editable_get_position (GTK_EDITABLE (entry)), ==, 20);

	set_text ("cat unique-fi");
	gtk_editable_select_region (GTK_EDITABLE (entry), 4, 10);
	key (GDK_Escape, 0);
	assert_entry ("cat unique-fi");
	gint start, end;
	g_assert_true (gtk_editable_get_selection_bounds (GTK_EDITABLE (entry), &start, &end));
	g_assert_cmpint (start, ==, 4);
	g_assert_cmpint (end, ==, 10);
	/* Compute GTK's expected forward/backward focus, then exercise Tab itself. */
	g_signal_emit_by_name (app.main_window, "move-focus", GTK_DIR_TAB_FORWARD);
	GtkWidget *next = gtk_window_get_focus (GTK_WINDOW (app.main_window));
	g_assert_true (next != entry);
	gtk_widget_grab_focus (entry);
	key (GDK_Tab, 0);
	g_assert_true (gtk_window_get_focus (GTK_WINDOW (app.main_window)) == next);
	g_assert_cmpstr (gtk_entry_get_text (GTK_ENTRY (entry)), ==, "cat unique-fi");
	gtk_widget_grab_focus (entry);
	g_signal_emit_by_name (app.main_window, "move-focus", GTK_DIR_TAB_BACKWARD);
	GtkWidget *previous = gtk_window_get_focus (GTK_WINDOW (app.main_window));
	g_assert_true (previous != entry);
	gtk_widget_grab_focus (entry);
	key (GDK_ISO_Left_Tab, GDK_SHIFT_MASK);
	g_assert_true (gtk_window_get_focus (GTK_WINDOW (app.main_window)) == previous);

	/* Directory entries retain their normal bindings. A global Tab override
	 * intentionally applies to them, so only check the ordinary profile here. */
	if (g_strcmp0 (g_getenv ("E2_COMMAND_KEYS_CASE"), "existing profile with global Tab binding") != 0)
	{
		GtkWidget *command_entry = entry;
		entry = gtk_bin_get_child (GTK_BIN (directory->combo));
		gchar *prefix = g_build_filename (g_getenv ("E2_COMMAND_KEYS_TEST"), "source", "folder-na", NULL);
		gchar *expected = g_strconcat (prefix, "me/", NULL);
		complete (prefix, expected);
		g_free (prefix);
		g_free (expected);
		entry = command_entry;
	}

	/* Re-registering configurable bindings must preserve the entry's mode. */
	e2_keybinding_register_all ();
	gtk_widget_grab_focus (entry);
	key (GDK_Tab, 0);
	g_assert_true (gtk_window_get_focus (GTK_WINDOW (app.main_window)) != entry);
	gtk_widget_grab_focus (entry);
	key (GDK_Escape, 0);
	complete ("cat unique-fi", "cat unique-file.txt ");
	/* Two Escapes restore the same mode, and modified Escape does not toggle. */
	key (GDK_Escape, 0);
	key (GDK_Escape, 0);
	key (GDK_Escape, GDK_SHIFT_MASK);
	complete ("cat unique-fi", "cat unique-file.txt ");

	/* Escape in the file list must not change the command entry's mode. */
	gtk_widget_grab_focus (curr_view->treeview);
	key (GDK_Escape, 0);
	complete ("cat unique-fi", "cat unique-file.txt ");

	/* Text selection and containing widgets can own a GTK grab. Completion
	 * must remain active even after entry keybindings have been reloaded. */
	GtkWidget *grabs[] = {entry, gtk_widget_get_parent (entry), app.main_window};
	for (guint i = 0; i < G_N_ELEMENTS (grabs); i++)
	{
		gtk_grab_add (grabs[i]);
		complete ("cat unique-fi", "cat unique-file.txt ");
		complete ("cat no-such-completion", "cat no-such-completion");
		key (GDK_Escape, 0);
		key (GDK_Tab, 0);
		g_assert_true (gtk_window_get_focus (GTK_WINDOW (app.main_window)) != entry);
		gtk_grab_remove (grabs[i]);
		gtk_widget_grab_focus (entry);
		key (GDK_Escape, 0);
		complete ("cat unique-fi", "cat unique-file.txt ");
	}

	/* A history popup owns its Escape key; closing it must not toggle Tab. */
	e2_combobox_prepend_history (command->combo, "cat history-entry", 10, FALSE);
	set_text ("cat unique-fi");
	gtk_combo_box_popup (GTK_COMBO_BOX (command->combo));
	gboolean shown = FALSE;
	g_object_get (command->combo, "popup-shown", &shown, NULL);
	g_assert_true (shown);
	g_assert_nonnull (gtk_grab_get_current ());
	key (GDK_Escape, 0);
	g_object_get (command->combo, "popup-shown", &shown, NULL);
	g_assert_false (shown);
	assert_entry ("cat unique-fi");
	complete ("cat unique-fi", "cat unique-file.txt ");

	g_print ("command-line completion and Escape navigation toggle passed\n");
	OPENBGL
	e2_action_run_simple_from ("command.quit", NULL, app.main_window);
	return FALSE;
}

__attribute__((constructor)) static void schedule (void)
{
	unsetenv ("LD_PRELOAD");
	g_timeout_add (500, tick, NULL);
}
