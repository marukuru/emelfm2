/* Clearing the command log while a running command prints ANSI-styled output
 * must not leave that output using text marks of the discarded buffer. */
#include "emelfm2.h"
#include "e2_action.h"
#include "e2_command.h"
#include "e2_output.h"
#include <glib/gstdio.h>

static guint step;

static gboolean exists (const gchar *name)
{
	gchar *path = g_build_filename (g_getenv ("E2_OUTPUT_CLEAR_TEST"), name, NULL);
	gboolean found = g_file_test (path, G_FILE_TEST_EXISTS);
	g_free (path);
	return found;
}

static gboolean tick (gpointer unused)
{
	if (curr_view == NULL || curr_view->dir[0] == '\0'
		|| g_atomic_int_get (&curr_view->listcontrols.cd_working)) return TRUE;
	switch (step)
	{
		case 0:
		{
			gchar *script = g_build_filename (g_getenv ("E2_OUTPUT_CLEAR_TEST"), "styled.sh", NULL);
			gchar *command = g_strdup_printf ("%s '%s'", script, g_getenv ("E2_OUTPUT_CLEAR_TEST"));
			CLOSEBGL
			e2_command_run_at (command, curr_view->dir, E2_COMMAND_RANGE_DEFAULT, app.main_window);
			OPENBGL
			g_free (command);
			g_free (script);
			step++;
			return TRUE;
		}
		case 1:
			if (!exists ("started")) return TRUE;
			/* Styled output for this command is in progress: discard the log. */
			e2_action_run_simple_from ("output.clear", NULL, app.main_window);
			step++;
			return TRUE;
		case 2:
			if (!exists ("finished")) return TRUE;
			step++;
			return TRUE;	/* let the watch thread print the remaining output */
		case 3:
		{
			CLOSEBGL
			GtkTextIter start, end;
			gtk_text_buffer_get_bounds (app.tab.buffer, &start, &end);
			gchar *text = gtk_text_buffer_get_text (app.tab.buffer, &start, &end, FALSE);
			gboolean done = strstr (text, "red 59 plain") != NULL;
			if (done)
			{
				g_assert_null (strstr (text, "red 0 plain"));
				g_assert_null (strstr (text, "\033"));
				fprintf (stderr, "styled output after clearing the log passed\n");
				gchar *passed = g_build_filename (g_getenv ("E2_OUTPUT_CLEAR_TEST"), "passed", NULL);
				g_file_set_contents (passed, "", 0, NULL);
				g_free (passed);
			}
			g_free (text);
			OPENBGL
			if (!done) return TRUE;
			e2_action_run_simple_from ("command.quit", NULL, app.main_window);
			return FALSE;
		}
	}
	return TRUE;
}

__attribute__((constructor)) static void schedule (void)
{
	unsetenv ("LD_PRELOAD");
	g_timeout_add (100, tick, NULL);
}
