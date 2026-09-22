/* Exercise real command dispatch and the resulting Command log. */
#include "emelfm2.h"
#include "e2_command.h"
#include "e2_option.h"
#include "e2_action.h"
#include <glib/gstdio.h>
#include <dlfcn.h>

static guint step;
static gboolean waiting;
static gint64 deadline;
static const gchar *root;
static gboolean fallback;

/* PTY exhaustion must leave ordinary command capture usable. */
int posix_openpt (int flags)
{
    if (!g_strcmp0 (g_getenv ("E2_COMMAND_PROGRESS_FALLBACK"), "1"))
    { errno = ENOSPC; return -1; }
    int (*open_terminal) (int) = dlsym (RTLD_NEXT, "posix_openpt");
    return open_terminal (flags);
}

static gchar *log_text (void)
{
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds (app.tab.buffer, &start, &end);
    return gtk_text_buffer_get_text (app.tab.buffer, &start, &end, FALSE);
}
static void contains (const gchar *text, const gchar *needle)
{
    if (strstr (text, needle) == NULL) g_error ("Missing %s in command log:\n%s", needle, text);
}
static gboolean tick (gpointer unused)
{
    if (curr_view == NULL || curr_view->dir[0] == 0
        || g_atomic_int_get (&curr_view->listcontrols.cd_working)) return TRUE;
    CLOSEBGL
    if (waiting)
    {
        gchar *text = log_text ();
        const gchar *finished = strstr (text, "returned '");
        if (finished == NULL)
        {
            if (g_get_monotonic_time () > deadline) g_error ("Command %u timed out:\n%s", step, text);
            g_free (text);
            OPENBGL
            return TRUE;
        }
        if (step < 3)
        {
            contains (text, "Counting objects:");
            contains (text, "Compressing objects:");
            contains (text, "Receiving objects: 100%");
            contains (text, "Resolving deltas: 100%");
            contains (text, "returned '0'");
            g_assert_true (strstr (text, "Receiving objects: 100%") < finished);
        }
        else if (step < 5)
        {
            contains (text, fallback ? "stderr-is-pipe" : "stderr-is-terminal");
            contains (text, "stdin-is-pipe");
            contains (text, "stdout-is-pipe");
            contains (text, "progress-complete");
            g_assert_null (strstr (text, "progress-start"));
            contains (text, "stdout-tail");
            contains (text, "stderr-tail");
            contains (text, "returned '7'");
            g_assert_true (strstr (text, "stderr-tail") < finished);
        }
        else if (step == 5)
        {
            contains (text, "redirect-ok");
            contains (text, "returned '0'");
            gchar *path = g_build_filename (root, "redirected", NULL), *data;
            g_assert_true (g_file_get_contents (path, &data, NULL, NULL));
            g_assert_cmpstr (data, ==, "redirected-error\n");
            g_free (path); g_free (data);
            //The command itself contains the marker, so check whole lines.
            g_assert_null (strstr (text, "\nredirected-error\n"));
        }
        else if (step == 6)
        {
            g_assert_null (strstr (text, "Receiving objects:"));
            contains (text, "returned '0'");
        }
        fprintf (stderr, "command progress: step %u passed\n", step);
        g_free (text);
        waiting = FALSE;
        step++;
    }
    if (root == NULL)
    {
        root = g_getenv ("E2_COMMAND_PROGRESS_TEST");
        fallback = !g_strcmp0 (g_getenv ("E2_COMMAND_PROGRESS_FALLBACK"), "1");
        if (fallback) step = 3;
        e2_option_bool_set ("fileop-show", TRUE);
    }
    if (step == 7 || (fallback && step == 6))
    {
        gchar *done = g_build_filename (root, "passed", NULL);
        g_assert_true (g_file_set_contents (done, "passed", -1, NULL));
        g_free (done);
        e2_main_closedown (TRUE, TRUE, TRUE);
        return FALSE;
    }
    e2_action_run_simple_from ("output.clear", NULL, app.main_window);
    gchar *command;
    if (step < 3)
        command = g_strdup_printf ("%sgit clone --no-local '%s/origin' '%s/clone-%u'",
            step == 1 ? ">" : step == 2 ? "|" : "", root, root, step);
    else if (step < 5)
        command = g_strdup_printf ("%spython3 '%s/probe.py'", step == 4 ? "|" : "", root);
    else if (step == 5)
        command = g_strdup ("sh -c 'echo redirected-error >&2; echo redirect-ok' 2>redirected | cat");
    else
        command = g_strdup_printf ("git clone --quiet --no-local '%s/origin' '%s/clone-quiet'", root, root);
    waiting = TRUE;
    deadline = g_get_monotonic_time () + 20000000;
    e2_command_run_at (command, root, E2_COMMAND_RANGE_DEFAULT, app.main_window);
    g_free (command);
    OPENBGL
    return TRUE;
}
__attribute__((constructor)) static void schedule (void)
{ unsetenv ("LD_PRELOAD"); g_timeout_add (100, tick, NULL); }
