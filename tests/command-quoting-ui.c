/* Run pasted commands through the command entry and inspect the Commandlog. */
#include "emelfm2.h"
#include "e2_command.h"
#include "e2_command_line.h"
#include "e2_action.h"
#include "e2_option.h"
#include <glib/gstdio.h>

static guint step;
static gboolean waiting;
static gint64 deadline;
static const gchar *root;
static gchar *kind, *expected;
static gsize expected_length;
static gchar *read_case (const gchar *suffix, gsize *length)
{
	gchar *path = g_strdup_printf ("%s/%u.%s", root, step, suffix);
	gchar *text;
	g_assert_true (g_file_get_contents (path, &text, length, NULL));
	g_free (path);
	return text;
}
static gchar *log_text (void)
{
	GtkTextIter start, end;
	gtk_text_buffer_get_bounds (app.tab.buffer, &start, &end);
	return gtk_text_buffer_get_text (app.tab.buffer, &start, &end, FALSE);
}
static gboolean tick (gpointer unused)
{
	if (curr_view == NULL || !curr_view->dir[0]
		|| g_atomic_int_get (&curr_view->listcontrols.cd_working)) return TRUE;
	CLOSEBGL
	if (root == NULL)
	{
		root = g_getenv ("E2_COMMAND_QUOTING_TEST");
	}
	if (waiting)
	{
		gchar *log = log_text ();
		gboolean complete = FALSE;
		if (!strcmp (kind, "cd")) complete = !strcmp (curr_view->dir, expected);
		else if (!strcmp (kind, "error")) complete = strstr (log, expected) != NULL;
		else if (strstr (log, "returned '0'") != NULL)
		{
			gchar *path = g_strdup_printf ("%s/%u.actual", root, step), *actual;
			gsize length;
			if (g_file_get_contents (path, &actual, &length, NULL))
			{
				g_assert_cmpmem (actual, length, expected, expected_length);
				g_free (actual);
				complete = TRUE;
			}
			g_free (path);
		}
		g_assert_null (strstr (log, "__e2_ansi_")); //no internal markers in the log
		if (!complete)
		{
			if (g_get_monotonic_time () >= deadline)
				g_error ("Case %u timed out; directory %s; log:\n%s", step, curr_view->dir, log);
			g_free (log);
			OPENBGL
			return TRUE;
		}
		fprintf (stderr, "command quoting: case %u passed\n", step);
		g_free (log);
		g_free (kind);
		g_free (expected);
		waiting = FALSE;
		step++;
	}
	gchar *next = g_strdup_printf ("%s/%u.command", root, step);
	gboolean more = g_file_test (next, G_FILE_TEST_EXISTS);
	g_free (next);
	if (!more)
	{
		gchar *done = g_build_filename (root, "passed", NULL);
		g_assert_true (g_file_set_contents (done, "passed", -1, NULL));
		g_free (done);
		e2_main_closedown (TRUE, TRUE, TRUE);
		return FALSE;
	}
	kind = read_case ("kind", NULL);
	expected = read_case ("expected", &expected_length);
	gchar *command = read_case ("command", NULL);
	e2_action_run_simple_from ("output.clear", NULL, app.main_window);
	E2_CommandLineRuntime *rt = NULL;
	for (GList *p = app.command_lines; p != NULL; p = p->next)
		if (((E2_CommandLineRuntime *)p->data)->original) { rt = p->data; break; }
	g_assert_nonnull (rt);
	GtkWidget *entry = gtk_bin_get_child (GTK_BIN (rt->combo));
	gtk_entry_set_text (GTK_ENTRY (entry), command);
	waiting = TRUE;
	deadline = g_get_monotonic_time () + 15000000;
	g_signal_emit_by_name (entry, "activate");
	g_free (command);
	OPENBGL
	return TRUE;
}
__attribute__((constructor)) static void schedule (void)
{ unsetenv ("LD_PRELOAD"); g_timeout_add (100, tick, NULL); }
