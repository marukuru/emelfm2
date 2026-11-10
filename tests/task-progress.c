/* Drive the progress window of one copy or move task. Reading files named
 * *.slow is slowed down, so that the task can be paused, resumed or cancelled.
 * The runner prepares the directories and checks the results. */
#include "emelfm2.h"
#include "e2_action.h"
#include "e2_filestore.h"
#include "e2_pane.h"
#include "e2_task.h"
#include <dlfcn.h>

extern pthread_mutex_t task_mutex;

static guint step;
static gboolean seen;	/* the progress window was shown */
static gboolean paused;	/* a pause stopped the copy, until resumed */
static gdouble pause_fraction;
static gint64 pause_size, pause_time;
static gint actions;	/* 0 waiting, 1 pausing, 2 resumed or cancelled */

ssize_t read (int fd, void *buf, size_t count)
{
	static ssize_t (*real_read) (int, void *, size_t);
	if (real_read == NULL)
		real_read = dlsym (RTLD_NEXT, "read");
	gchar link[64], target[PATH_MAX];
	g_snprintf (link, sizeof (link), "/proc/self/fd/%d", fd);
	ssize_t length = readlink (link, target, sizeof (target) - 1);
	if (length > 0)
	{
		target[length] = '\0';
		if (g_str_has_suffix (target, ".slow"))
			g_usleep (20000);
	}
	return real_read (fd, buf, count);
}

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

static GtkWidget *progress_window (void)
{
	GList *windows = gtk_window_list_toplevels (), *link;
	GtkWidget *found = NULL;
	for (link = windows; link != NULL; link = link->next)
		if (GTK_IS_DIALOG (link->data) && gtk_widget_get_visible (link->data)
			&& !g_strcmp0 (gtk_widget_get_name (link->data), "task-progress"))
			found = link->data;
	g_list_free (windows);
	return found;
}

static GtkWidget *find_child (GtkWidget *widget, GType type)
{
	if (G_TYPE_CHECK_INSTANCE_TYPE (widget, type))
		return widget;
	GtkWidget *found = NULL;
	if (GTK_IS_CONTAINER (widget))
	{
		GList *children = gtk_container_get_children (GTK_CONTAINER (widget)), *link;
		for (link = children; link != NULL && found == NULL; link = link->next)
			found = find_child (link->data, type);
		g_list_free (children);
	}
	return found;
}

/* TRUE if some label in widget contains text */
static gboolean shows (GtkWidget *widget, const gchar *text)
{
	if (GTK_IS_LABEL (widget))
		return strstr (gtk_label_get_text (GTK_LABEL (widget)), text) != NULL;
	gboolean found = FALSE;
	if (GTK_IS_CONTAINER (widget))
	{
		GList *children = gtk_container_get_children (GTK_CONTAINER (widget)), *link;
		for (link = children; link != NULL && !found; link = link->next)
			found = shows (link->data, text);
		g_list_free (children);
	}
	return found;
}

static gint64 watched_size (void)
{
	struct stat sb;
	return (stat (g_getenv ("E2_PG_WATCH"), &sb) == 0) ? (gint64) sb.st_size : -1;
}

/* Act on the progress window as requested, once the copy has made progress. */
static gboolean watch (gpointer unused)
{
	CLOSEBGL
	GtkWidget *dialog = progress_window ();
	if (dialog != NULL)
	{
		seen = TRUE;
		/* the file being copied is named */
		g_assert_true (shows (dialog, ".slow"));
		gdouble fraction = gtk_progress_bar_get_fraction (
			GTK_PROGRESS_BAR (find_child (dialog, GTK_TYPE_PROGRESS_BAR)));
		const gchar *request = g_getenv ("E2_PG_DO");
		gint64 now = g_get_monotonic_time ();
		if (actions == 0 && fraction > 0.05 && !strcmp (request, "pause"))
		{
			gtk_dialog_response (GTK_DIALOG (dialog), E2_RESPONSE_USER1);
			actions = 1;
			pause_time = now;
		}
		else if (actions == 1 && pause_size < 0 && now - pause_time > 200000)
		{	/* the task has had time to reach its waiting point */
			pause_size = watched_size ();
			pause_fraction = fraction;
			pause_time = now;
		}
		else if (actions == 1 && pause_size >= 0 && now - pause_time > 400000)
		{
			paused = (watched_size () == pause_size && fraction == pause_fraction);
			gtk_dialog_response (GTK_DIALOG (dialog), E2_RESPONSE_USER2);
			actions = 2;
		}
		else if (actions == 0 && fraction > 0.05 && !strcmp (request, "cancel"))
		{
			gtk_dialog_response (GTK_DIALOG (dialog), E2_RESPONSE_NOTOALL);
			actions = 2;
		}
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
			gchar **names = g_strsplit (g_getenv ("E2_PG_SELECT"), ",", -1);
			for (guint i = 0; names[i] != NULL; i++)
				e2_fileview_focus_row (curr_view, row_for (curr_view, names[i]),
					TRUE, i == 0, FALSE, FALSE);
			g_strfreev (names);
			OPENBGL
			pause_size = -1;
			gchar *action = g_strconcat (_A(6), ".", g_getenv ("E2_PG_ACTION"), NULL);
			g_assert_nonnull (e2_action_get (action));
			g_timeout_add (20, watch, NULL);
			e2_action_run_simple_from (action, NULL, app.main_window);
			g_free (action);
			step++;
			return TRUE;
		}
		case 1:
			if (!tasks_done ()) return TRUE;
			step++;
			return TRUE;
		default:
		{
			step = 3;
			CLOSEBGL
			/* the window is gone when the task has finished */
			g_assert_null (progress_window ());
			OPENBGL
			gchar *result = g_strdup_printf ("window %d paused %d\n", seen, paused);
			g_assert_true (g_file_set_contents (g_getenv ("E2_PG_RESULT"), result, -1, NULL));
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
