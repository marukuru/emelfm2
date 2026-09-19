/* Loaded by run-single-instance.sh into the primary process only. */
#include "emelfm2.h"
#include <glib/gstdio.h>

static gboolean observe (gpointer data)
{
	static gboolean hidden;
	if (app.main_window == NULL || curr_view == NULL
		|| curr_view->dir[0] == '\0'
		|| g_atomic_int_get (&app.pane1.view.listcontrols.cd_working)
		|| g_atomic_int_get (&app.pane2.view.listcontrols.cd_working)) return TRUE;
	const gchar *root = g_getenv ("E2_INSTANCE_TEST");
	CLOSEBGL
	if (!hidden)
	{
		gtk_widget_hide (app.main_window);
		gchar *path = g_build_filename (root, "ready", NULL);
		g_file_set_contents (path, "ready", -1, NULL);
		g_free (path);
		hidden = TRUE;
	}
	else if (gtk_widget_get_visible (app.main_window))
	{
		gchar *path = g_build_filename (root, "restored", NULL);
		g_file_set_contents (path, "restored", -1, NULL);
		g_free (path);
		OPENBGL
		return FALSE;
	}
	OPENBGL
	return TRUE;
}
__attribute__((constructor)) static void schedule (void)
{
	unsetenv ("LD_PRELOAD");
	g_timeout_add (50, observe, NULL);
}
