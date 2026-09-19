/* Single-instance activation. Licensed under GPL version 3 or later.
 * A profile-local advisory lock elects the owner; a FIFO wakes its main loop.
 * This also works without a session bus, on both GTK generations. */
#include "e2_single_instance.h"
#include "e2_cl_option.h"
#include "e2_option.h"
#include "e2_tray.h"
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

static gint owner_fd = -1;
static GIOChannel *activation_channel;
static guint activation_watch;

static gboolean _e2_single_instance_activate (GIOChannel *channel,
	GIOCondition condition, gpointer data)
{
	gchar requests[256];
	if (read (g_io_channel_unix_get_fd (channel), requests, sizeof (requests)) > 0
		&& app.main_window != NULL)
	{
		CLOSEBGL
		e2_tray_show_main ();
		OPENBGL
	}
	return TRUE;
}

void e2_single_instance_cleanup (void)
{
	if (activation_watch != 0)
		g_source_remove (activation_watch);
	activation_watch = 0;
	if (activation_channel != NULL)
		g_io_channel_unref (activation_channel);
	activation_channel = NULL;
	if (owner_fd >= 0)
		close (owner_fd);
	owner_fd = -1;
	/* Keep the lock inode and FIFO: unlinking can split simultaneous starters
	 * between different inodes. The kernel releases the lock even after a crash. */
}

static gboolean _e2_single_instance_claim (gboolean activate)
{
	if (!e2_option_bool_get ("single-instance"))
	{
		e2_single_instance_cleanup ();
		return TRUE;
	}
	if (owner_fd >= 0)
		return TRUE;

	gchar *lockpath = g_build_filename (e2_cl_options.config_dir,
		".instance-lock", NULL);
	gchar *fifopath = g_build_filename (e2_cl_options.config_dir,
		".instance-activate", NULL);
	gint flags = O_RDWR | O_NONBLOCK;
#ifdef O_NOFOLLOW
	flags |= O_NOFOLLOW;
#endif
	gint lockfd = open (lockpath, flags | O_CREAT, 0600);
	g_free (lockpath);
	gint pipefd = -1;
	struct stat st;
	if (lockfd < 0 || fstat (lockfd, &st) != 0 || !S_ISREG (st.st_mode)
		|| st.st_uid != geteuid ())
		goto unavailable;
	fcntl (lockfd, F_SETFD, FD_CLOEXEC);
	if (mkfifo (fifopath, 0600) != 0 && errno != EEXIST)
		goto unavailable;
	pipefd = open (fifopath, flags);
	if (pipefd < 0 || fstat (pipefd, &st) != 0 || !S_ISFIFO (st.st_mode)
		|| st.st_uid != geteuid ())
		goto unavailable;
	fcntl (pipefd, F_SETFD, FD_CLOEXEC);
	if (flock (lockfd, LOCK_EX | LOCK_NB) != 0)
	{
		if (errno != EWOULDBLOCK && errno != EAGAIN)
			goto unavailable;
		/* O_RDWR keeps a reader alive even if the owner exits at this instant,
		 * so a secondary launch cannot receive SIGPIPE. A full pipe already
		 * contains a request to present the window. */
		if (activate)
		{
			ssize_t sent;
			do { sent = write (pipefd, "A", 1); } while (sent < 0 && errno == EINTR);
		}
		close (pipefd);
		close (lockfd);
		g_free (fifopath);
		return FALSE;
	}
	owner_fd = lockfd;
	activation_channel = g_io_channel_unix_new (pipefd);
	g_io_channel_set_close_on_unref (activation_channel, TRUE);
	activation_watch = g_io_add_watch (activation_channel, G_IO_IN,
		_e2_single_instance_activate, NULL);
	g_free (fifopath);
	return TRUE;

unavailable:
	if (pipefd >= 0) close (pipefd);
	if (lockfd >= 0) close (lockfd);
	g_free (fifopath);
	/* An unwritable profile must not prevent the file manager from opening. */
	return TRUE;
}

gboolean e2_single_instance_start (void)
{
	return _e2_single_instance_claim (TRUE);
}

void e2_single_instance_sync (void)
{
	/* Applying preferences never closes a window that is already open. */
	_e2_single_instance_claim (FALSE);
}
