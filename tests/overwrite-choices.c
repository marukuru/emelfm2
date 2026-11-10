/* Drive one copy or move through the overwrite dialog's Keep all and Merge
 * choices. The runner prepares the directories and checks the results. */
#include "emelfm2.h"
#include "e2_action.h"
#include "e2_dialog.h"
#include "e2_filestore.h"
#include "e2_pane.h"
#include "e2_task.h"
#include <dlfcn.h>

extern pthread_mutex_t task_mutex;

/* Make removal of items with a given name suffix fail, as for an item in a
 * directory owned by someone else. Forked children inherit this. */
static gboolean undeletable (const char *path)
{
	const gchar *suffix = g_getenv ("E2_OW_UNDELETABLE");
	if (suffix == NULL || !g_str_has_suffix (path, suffix))
		return FALSE;
	errno = EACCES;
	return TRUE;
}

int unlink (const char *path)
{
	static int (*real_unlink) (const char *);
	if (real_unlink == NULL)
		real_unlink = dlsym (RTLD_NEXT, "unlink");
	return undeletable (path) ? -1 : real_unlink (path);
}

/* the recursive delete uses remove(), which calls unlink() within libc */
int remove (const char *path)
{
	static int (*real_remove) (const char *);
	if (real_remove == NULL)
		real_remove = dlsym (RTLD_NEXT, "remove");
	return undeletable (path) ? -1 : real_remove (path);
}

static guint step;
static guint dialogs;	/* overwrite dialogs shown during the task */

static gint row_for (ViewInfo *view, const gchar *name)
{
	GtkTreeIter iter;
	gint row = 0;
	g_assert_true (gtk_tree_model_get_iter_first (view->model, &iter));
	do
	{
		FileInfo *info;
		gtk_tree_model_get (view->model, &iter, FINFO, &info, -1);
		if (!strcmp (info->filename, name)) return row;
		row++;
	} while (gtk_tree_model_iter_next (view->model, &iter));
	g_error ("%s is not listed", name);
	return -1;
}

static GtkWidget *overwrite_dialog (void)
{
	GList *windows = gtk_window_list_toplevels (), *link;
	GtkWidget *found = NULL;
	for (link = windows; link != NULL; link = link->next)
		if (GTK_IS_DIALOG (link->data) && gtk_widget_get_visible (link->data)
			&& !g_strcmp0 (gtk_window_get_title (link->data), "Overwrite confirmation")
			/* an answered dialog may remain until the task thread resumes */
			&& g_object_get_data (link->data, "e2-test-answered") == NULL)
			found = link->data;
	g_list_free (windows);
	return found;
}

static gboolean sensitive (GtkWidget *dialog, gint response)
{
	GtkWidget *button = gtk_dialog_get_widget_for_response (GTK_DIALOG (dialog), response);
	g_assert_nonnull (button);
	return gtk_widget_get_sensitive (button);
}

static gint response_for (const gchar *name)
{
	if (!strcmp (name, "keepall")) return E2_RESPONSE_KEEPALL;
	if (!strcmp (name, "merge")) return E2_RESPONSE_MERGE;
	if (!strcmp (name, "keep")) return GTK_RESPONSE_NO;
	g_error ("unknown response %s", name);
	return 0;
}

/* Answer each overwrite dialog: the first one as requested, any later one
 * (there should be none after Keep all or Merge) with Keep. */
static gboolean answer (gpointer unused)
{
	CLOSEBGL
	GtkWidget *dialog = overwrite_dialog ();
	if (dialog != NULL)
	{
		dialogs++;
		gboolean keepall = sensitive (dialog, E2_RESPONSE_KEEPALL);
		gboolean merge = sensitive (dialog, E2_RESPONSE_MERGE);
		fprintf (stderr, "dialog %u: keep all %d, merge %d\n", dialogs, keepall, merge);
		gint response = GTK_RESPONSE_NO;
		if (dialogs == 1)
		{
			g_assert_cmpint (keepall, ==, atoi (g_getenv ("E2_OW_KEEPALL")));
			g_assert_cmpint (merge, ==, atoi (g_getenv ("E2_OW_MERGE")));
			response = response_for (g_getenv ("E2_OW_RESPONSE"));
		}
		g_object_set_data (G_OBJECT (dialog), "e2-test-answered", GINT_TO_POINTER (1));
		gtk_dialog_response (GTK_DIALOG (dialog), response);
	}
	OPENBGL
	return step < 3;
}

static gboolean tasks_done (void)
{
	gboolean done = TRUE;
	pthread_mutex_lock (&task_mutex);
	for (GList *link = app.taskhistory; link != NULL; link = link->next)
		if (((E2_TaskRuntime *)link->data)->status < E2_TASK_COMPLETED)
			done = FALSE;
	pthread_mutex_unlock (&task_mutex);
	return done;
}

static gboolean tick (gpointer unused)
{
	if (curr_view == NULL || !app.pane1.view.dir[0] || !app.pane2.view.dir[0]
		|| g_atomic_int_get (&app.pane1.view.listcontrols.cd_working)
		|| g_atomic_int_get (&app.pane2.view.listcontrols.cd_working)) return TRUE;
	switch (step)
	{
		case 0:
		{
			CLOSEBGL
			if (curr_view != &app.pane1.view)
				e2_pane_activate_other ();
			gchar **names = g_strsplit (g_getenv ("E2_OW_SELECT"), ",", -1);
			for (guint i = 0; names[i] != NULL; i++)
				e2_fileview_focus_row (curr_view, row_for (curr_view, names[i]),
					TRUE, i == 0, FALSE, FALSE);
			g_strfreev (names);
			OPENBGL
			gchar *action = g_strconcat (_A(6), ".",
				!strcmp (g_getenv ("E2_OW_ACTION"), "move") ? _A(65) : _A(39), NULL);
			g_timeout_add (20, answer, NULL);
			e2_action_run_simple_from (action, NULL, app.main_window);
			g_free (action);
			step++;
			return TRUE;
		}
		case 1:
			if (!tasks_done ()) return TRUE;
			step++;
			return TRUE;	/* let any late dialog be counted */
		default:
		{
			step = 3;
			gchar *result = g_strdup_printf ("dialogs %u\n", dialogs);
			g_assert_true (g_file_set_contents (g_getenv ("E2_OW_RESULT"), result, -1, NULL));
			g_free (result);
			e2_action_run_simple_from ("command.quit", NULL, app.main_window);
			return FALSE;
		}
	}
}

__attribute__((constructor)) static void schedule (void)
{
	unsetenv ("LD_PRELOAD");
	g_timeout_add (100, tick, NULL);
}
