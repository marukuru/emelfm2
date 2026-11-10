/* Progress window for copy and move tasks. The work stays in the task thread:
 * the file copier reports each copied block, and may be paused or abandoned
 * from here. The window is shown only when a task is not quickly completed.
 * The window is adapted from the former copy and move plugins, portions of
 * which are copyright (C) 2004 Florian Zaehringer <flo.zaehringer@web.de>.
 * Released under the GNU General Public License, version 3 or later. */
#include "emelfm2.h"
#include <string.h>
#include <pthread.h>
#include "e2_task_progress.h"
#include "e2_dialog.h"
#include "e2_filestore.h"
#include "e2_icons.h"
#include "e2_tray.h"

//no window for tasks which finish within this time (usec)
#define E2_PROGRESS_DELAY 500000
//minimum interval (usec) between window updates
#define E2_PROGRESS_INTERVAL 100000
//max. no. of bytes shown at the end of a path in the window
#define E2_PROGRESS_PATHLEN 55

enum
{
	E2_PROGRESS_PAUSE = 1,	//the task waits until resumed
	E2_PROGRESS_STOP = 1 << 1,	//the user abandoned the task
};

struct _E2_TaskProgress
{
	E2_ActionTaskData *qed;
	gboolean move;
	gchar *destdir;	//localised path of destination dir, with trailer
	guint count;	//no. of items to process
	guint index;	//0-based index of the item being processed
	GArray *starts;	//guint64 aggregate size of the items before each item, and
					//of all items, or NULL to show progress by item count
	guint64 current;	//amount copied for the current item
	gchar *src;	//localised path of item being processed, for display
	gchar *dest;	//localised path of its destination
	gboolean renamed;	//src or dest changed since the window was updated
	gint64 started;	//monotonic time since which the task has been working
	gint64 updated;	//monotonic time of the last window update
	gboolean created;	//the window has been created
	pthread_mutex_t lock;	//for flags, changed in the main thread
	pthread_cond_t resumed;
	gint flags;
	//these are changed only with BGL closed
	GtkWidget *dialog;
	GtkWidget *label;
	GtkWidget *bar;
	GtkWidget *pause_btn;
	GtkWidget *resume_btn;
};

//the progress data for the task run by the current thread, if any
static __thread E2_TaskProgress *active = NULL;

/**
@brief add the size of the item reported by a treewalk to @a size
@param localpath UNUSED absolute path of item reported by the walker
@param statptr pointer to struct stat with data about @a localpath
@param status code from the walker, indicating what type of report it is
@param size pointer to the size total

@return E2TW_CONTINUE always
*/
static E2_TwResult _e2_task_progress_twcb (VPATH *localpath,
	const struct stat *statptr, E2_TwStatus status, guint64 *size)
{
	switch (status)
	{
		case E2TW_F:	//not dir or link
		case E2TW_SL:	//symbolic link
		case E2TW_SLN:	//symbolic link naming non-existing file
			*size += statptr->st_size;
		default:
			break;
	}
	return E2TW_CONTINUE;
}
/**
@brief wait while the task is paused
Expects BGL open
@param pg pointer to progress data

@return FALSE if the user has abandoned the task
*/
static gboolean _e2_task_progress_wait (E2_TaskProgress *pg)
{
	pthread_mutex_lock (&pg->lock);
	if ((pg->flags & (E2_PROGRESS_PAUSE | E2_PROGRESS_STOP)) == E2_PROGRESS_PAUSE)
	{
		pthread_mutex_unlock (&pg->lock);
		//let the file panes show what has been done so far
		e2_filestore_enable_refresh ();
		*pg->qed->status = E2_TASK_PAUSED;
		pthread_mutex_lock (&pg->lock);
		pthread_cleanup_push ((gpointer)pthread_mutex_unlock, &pg->lock);
		while ((pg->flags & (E2_PROGRESS_PAUSE | E2_PROGRESS_STOP)) == E2_PROGRESS_PAUSE)
			pthread_cond_wait (&pg->resumed, &pg->lock);	//cancellation point
		pthread_cleanup_pop (1);
		*pg->qed->status = E2_TASK_RUNNING;
		e2_filestore_disable_refresh ();
		pthread_mutex_lock (&pg->lock);
	}
	gboolean go = !(pg->flags & E2_PROGRESS_STOP);
	pthread_mutex_unlock (&pg->lock);
	return go;
}
/**
@brief "response" signal callback for the progress window
@param dialog UNUSED the window where the response was generated
@param response the response returned from the dialog
@param pg pointer to progress data

@return
*/
static void _e2_task_progress_response_cb (GtkDialog *dialog, gint response,
	E2_TaskProgress *pg)
{
	gboolean pause;
	NEEDCLOSEBGL
	pthread_mutex_lock (&pg->lock);
	switch (response)
	{
		case E2_RESPONSE_USER1:
			pg->flags |= E2_PROGRESS_PAUSE;
			break;
		case E2_RESPONSE_USER2:
			pg->flags &= ~E2_PROGRESS_PAUSE;
			break;
		case E2_RESPONSE_NOTOALL:	//cancel
		case GTK_RESPONSE_NONE:	//window hidden or destroyed
		case GTK_RESPONSE_DELETE_EVENT:
			pg->flags = E2_PROGRESS_STOP;
			break;
		default:
			break;
	}
	pause = (pg->flags & E2_PROGRESS_PAUSE) != 0;
	gboolean stop = (pg->flags & E2_PROGRESS_STOP) != 0;
	pthread_cond_broadcast (&pg->resumed);
	pthread_mutex_unlock (&pg->lock);

	if (pg->dialog != NULL)
	{
		gtk_widget_set_sensitive (pg->pause_btn, !(pause || stop));
		gtk_widget_set_sensitive (pg->resume_btn, pause && !stop);
		if (!stop)
			gtk_widget_grab_focus ((pause) ? pg->resume_btn : pg->pause_btn);
	}
	NEEDOPENBGL
}
/**
@brief "destroy" signal callback for the progress window
@param dialog UNUSED the window being destroyed
@param pg pointer to progress data

@return
*/
static void _e2_task_progress_destroy_cb (GtkWidget *dialog, E2_TaskProgress *pg)
{
	//the window cannot be destroyed by the user, but just in case
	pg->dialog = NULL;
	pthread_mutex_lock (&pg->lock);
	pg->flags = E2_PROGRESS_STOP;
	pthread_cond_broadcast (&pg->resumed);
	pthread_mutex_unlock (&pg->lock);
}
/**
@brief create and show the progress window
Expects BGL closed
@param pg pointer to progress data

@return
*/
static void _e2_task_progress_create (E2_TaskProgress *pg)
{
	pg->dialog = e2_dialog_create ((pg->move) ? _("moving") : _("copying"),
		NULL, (ResponseFunc)_e2_task_progress_response_cb, pg, NULL);
	gtk_widget_set_name (pg->dialog, "task-progress");
	e2_dialog_setup (pg->dialog, app.main_window);
#ifndef USE_GTK3_0
	gtk_dialog_set_has_separator (GTK_DIALOG (pg->dialog), FALSE);
#endif
	GtkWidget *vbox =
#ifdef USE_GTK2_14
		gtk_dialog_get_content_area (GTK_DIALOG (pg->dialog));
#else
		GTK_DIALOG (pg->dialog)->vbox;
#endif
	pg->label = e2_widget_add_mid_label (vbox, "", 0.0, FALSE, 0);
	//room for a line with a shortened path, or a wrapped label would be narrow
	gtk_label_set_width_chars (GTK_LABEL (pg->label), E2_PROGRESS_PATHLEN + 10);
	pg->bar = gtk_progress_bar_new ();
#ifdef USE_GTK3_0
	gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR (pg->bar), TRUE);
#endif
	gtk_box_pack_start (GTK_BOX (vbox), pg->bar, TRUE, TRUE, E2_PADDING_LARGE);

	E2_Button button = { _("_Pause"), STOCK_NAME_MEDIA_PAUSE, NULL, 0, 0,
		E2_RESPONSE_USER1 };
	pg->pause_btn = e2_dialog_add_defined_button (pg->dialog, &button);
	button.label = _("_Resume");
	button.name = STOCK_NAME_MEDIA_PLAY;
	button.response = E2_RESPONSE_USER2;
	pg->resume_btn = e2_dialog_add_defined_button (pg->dialog, &button);
	gtk_widget_set_sensitive (pg->resume_btn, FALSE);
	button = E2_BUTTON_CANCEL;
	button.showflags |= E2_BTN_DEFAULT;
	e2_dialog_add_defined_button (pg->dialog, &button);
	e2_dialog_set_negative_response (pg->dialog, E2_BUTTON_CANCEL.response);
	g_signal_connect (G_OBJECT (pg->dialog), "destroy",
		G_CALLBACK (_e2_task_progress_destroy_cb), pg);

	gtk_widget_show_all (vbox);
	e2_tray_register_transfer (pg->dialog);
	if (!e2_tray_defer_dialog (pg->dialog, FALSE, FALSE))
		gtk_widget_show (pg->dialog);
}
/**
@brief get display form of @a localpath, shortened if need be
@param localpath localised path

@return newly-allocated UTF-8 string
*/
static gchar *_e2_task_progress_show_path (const gchar *localpath)
{
	gchar *utf = F_DISPLAYNAME_FROM_LOCALE ((gchar *)localpath);
	gchar *shown = e2_utils_str_shorten (utf, E2_PROGRESS_PATHLEN, E2_DOTS_START);
	F_FREE (utf, localpath);
	return shown;
}
/**
@brief create or update the progress window, unless that was done very recently
The window is not created until the task has been working for a while.
Expects BGL open
@param pg pointer to progress data

@return
*/
static void _e2_task_progress_update (E2_TaskProgress *pg)
{
	gint64 now = g_get_monotonic_time ();
	if (!pg->created && now - pg->started < E2_PROGRESS_DELAY)
		return;
	if (pg->created && now - pg->updated < E2_PROGRESS_INTERVAL)
		return;
	pg->updated = now;

	gdouble fraction;
	gchar *text = NULL;
	if (pg->starts != NULL)
	{
		guint64 before = g_array_index (pg->starts, guint64, pg->index);
		guint64 itemsize = g_array_index (pg->starts, guint64, pg->index + 1) - before;
		guint64 total = g_array_index (pg->starts, guint64, pg->count);
		guint64 done = before + MIN (pg->current, itemsize);
		fraction = (total > 0) ? (gdouble) done / total : (gdouble) pg->index / pg->count;
		text = g_strdup_printf (_("%.2f MB of %.2f MB  (%.0f%%)"),
			done / 1048576.0, total / 1048576.0, fraction * 100.0);
	}
	else
		fraction = (gdouble) pg->index / pg->count;

	gchar *label = NULL;
	if (pg->renamed)
	{
		pg->renamed = FALSE;
		gchar *shortsrc = _e2_task_progress_show_path (pg->src);
		gchar *destdir = g_path_get_dirname (pg->dest);
		gchar *shortdest = _e2_task_progress_show_path (destdir);
		gchar *num1 = g_strdup_printf ("%u", pg->index + 1);	//gettext workaround
		gchar *num2 = g_strdup_printf ("%u", pg->count);
		label = g_strdup_printf ((pg->move) ?
			_("moving %s\nto %s\nthis is item %s of %s"):
			_("copying %s\nto %s\nthis is item %s of %s"),
			shortsrc, shortdest, num1, num2);
		g_free (shortsrc);
		g_free (destdir);
		g_free (shortdest);
		g_free (num1);
		g_free (num2);
	}

	CLOSEBGL
	if (!pg->created)
	{
		pg->created = TRUE;
		_e2_task_progress_create (pg);
	}
	if (pg->dialog != NULL)
	{
		if (label != NULL)
			gtk_label_set_text (GTK_LABEL (pg->label), label);
		if (text != NULL)
			gtk_progress_bar_set_text (GTK_PROGRESS_BAR (pg->bar), text);
		gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR (pg->bar), CLAMP (fraction, 0.0, 1.0));
	}
	OPENBGL
	g_free (label);
	g_free (text);
}
/**
@brief record what is being done now
@param pg pointer to progress data
@param src localised path of the item being processed
@param dest localised path of its destination

@return
*/
static void _e2_task_progress_set_names (E2_TaskProgress *pg, const gchar *src,
	const gchar *dest)
{
	g_free (pg->src);
	g_free (pg->dest);
	pg->src = g_strdup (src);
	pg->dest = g_strdup (dest);
	pg->renamed = TRUE;
}
/**
@brief observer of file copies done in the task thread
@param src path of file being copied
@param dest path of the copy
@param bytes 0 when a file copy starts, otherwise the size of a copied block
@param pg pointer to progress data

@return FALSE to abandon the copy
*/
static gboolean _e2_task_progress_monitor (VPATH *src, VPATH *dest,
	guint64 bytes, E2_TaskProgress *pg)
{
	if (bytes == 0)
		_e2_task_progress_set_names (pg, VPSTR (src), VPSTR (dest));
	else
		pg->current += bytes;
	_e2_task_progress_update (pg);
	return _e2_task_progress_wait (pg);
}

  /******************/
 /***** public *****/
/******************/

/**
@brief setup progress reporting for the copy or move task run by the current thread
A move within a filesystem shows progress by item count, as its items are
just renamed. Otherwise, all items are scanned first, to get their total size.
Expects BGL open
@param qed pointer to task data, with the names of the items to be processed
@param destdir localised path of destination dir, with trailer
@param move TRUE for a move task, FALSE for a copy task

@return pointer to new progress data, to be freed by e2_task_progress_free()
*/
E2_TaskProgress *e2_task_progress_new (E2_ActionTaskData *qed,
	const gchar *destdir, gboolean move)
{
	E2_TaskProgress *pg = g_new0 (E2_TaskProgress, 1);
	pg->qed = qed;
	pg->move = move;
	pg->destdir = g_strdup (destdir);
	GPtrArray *names = (GPtrArray *)qed->names;
	pg->count = names->len;
	pthread_mutex_init (&pg->lock, NULL);
	pthread_cond_init (&pg->resumed, NULL);

	gboolean sizes = TRUE;
#ifdef E2_VFS
	VPATH sdata = { (gchar *)qed->currdir, qed->currspace };
	VPATH ddata = { destdir, qed->othrspace };
	VPATH *sp = &sdata, *dp = &ddata;
#else
	VPATH *sp = (gchar *)qed->currdir, *dp = destdir;
#endif
	if (move)
	{
		struct stat sb;
		if (!e2_fs_stat (sp, &sb E2_ERR_NONE()))
		{
			dev_t device = sb.st_dev;
			sizes = (e2_fs_stat (dp, &sb E2_ERR_NONE()) || sb.st_dev != device);
		}
	}
	if (sizes)
	{
		pg->starts = g_array_sized_new (FALSE, FALSE, sizeof (guint64), pg->count + 1);
		GString *path = g_string_sized_new (PATH_MAX);
		guint64 total = 0;
		guint i;
		for (i = 0; i < pg->count; i++)
		{
			g_array_append_val (pg->starts, total);
			E2_SelectedItemInfo *item = g_ptr_array_index (names, i);
			g_string_printf (path, "%s%s", (gchar *)qed->currdir, item->filename);
#ifdef E2_VFS
			sdata.path = path->str;
#else
			sp = path->str;
#endif
			e2_fs_tw (sp, _e2_task_progress_twcb, &total, -1, E2TW_PHYS E2_ERR_NONE());
		}
		g_array_append_val (pg->starts, total);
		g_string_free (path, TRUE);
	}
	pg->started = g_get_monotonic_time ();
	active = pg;
	e2_fs_set_copy_monitor ((E2_FsCopyMonitor)_e2_task_progress_monitor, pg);
	return pg;
}
/**
@brief start processing the selected item with @a index
Expects BGL open
@param pg pointer to progress data
@param index 0-based index of the item

@return FALSE if the user has abandoned the task
*/
gboolean e2_task_progress_item (E2_TaskProgress *pg, guint index)
{
	pg->index = MIN (index, pg->count - 1);
	pg->current = 0;
	E2_SelectedItemInfo *item = g_ptr_array_index ((GPtrArray *)pg->qed->names, pg->index);
	gchar *src = g_strconcat ((gchar *)pg->qed->currdir, item->filename, NULL);
	gchar *dest = g_strconcat (pg->destdir, item->filename, NULL);
	_e2_task_progress_set_names (pg, src, dest);
	g_free (src);
	g_free (dest);
	_e2_task_progress_update (pg);
	return _e2_task_progress_wait (pg);
}
/**
@brief note that the task has just waited for the user to answer a question
That time does not count towards the delay before the window is shown.
@param pg pointer to progress data

@return
*/
void e2_task_progress_answered (E2_TaskProgress *pg)
{
	if (!pg->created)
		pg->started = g_get_monotonic_time ();
}
/**
@brief check whether the user has abandoned the copy or move run by the current thread

@return TRUE if the task is to be stopped
*/
gboolean e2_task_progress_stopped (void)
{
	E2_TaskProgress *pg = active;
	if (pg == NULL)
		return FALSE;
	pthread_mutex_lock (&pg->lock);
	gboolean stop = (pg->flags & E2_PROGRESS_STOP) != 0;
	pthread_mutex_unlock (&pg->lock);
	return stop;
}
/**
@brief end progress reporting, and clean up
Expects BGL open
@param pg pointer to progress data

@return
*/
void e2_task_progress_free (E2_TaskProgress *pg)
{
	e2_fs_set_copy_monitor (NULL, NULL);
	active = NULL;
	if (pg->created)
	{
		CLOSEBGL
		if (pg->dialog != NULL)
		{
			g_signal_handlers_disconnect_by_func (G_OBJECT (pg->dialog),
				_e2_task_progress_destroy_cb, pg);
			gtk_widget_destroy (pg->dialog);
		}
		OPENBGL
	}
	if (pg->starts != NULL)
		g_array_free (pg->starts, TRUE);
	g_free (pg->destdir);
	g_free (pg->src);
	g_free (pg->dest);
	pthread_cond_destroy (&pg->resumed);
	pthread_mutex_destroy (&pg->lock);
	g_free (pg);
}
