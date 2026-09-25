/* Loaded only by run-application-smoke.sh. Exercise exported application APIs
 * after asynchronous initial file-pane loading has finished. */
#include "emelfm2.h"
#include "e2_command.h"
#include "e2_option.h"
#include "e2_action.h"
#include <gmodule.h>
#include <dlfcn.h>
#include <signal.h>

extern pthread_mutex_t task_mutex;

static gboolean signal_while_locked (gpointer data)
{
    const gchar *directory = g_getenv ("E2_SMOKE_DIR");
    gchar *path = g_build_filename (directory, "signal-requested", NULL);
    gboolean requested = g_file_test (path, G_FILE_TEST_EXISTS);
    g_free (path);
    if (!requested) return TRUE;
    /* Reproduce delivery while a main-thread task operation owns its lock.
     * Cleanup in a raw signal handler deadlocks here. */
    pthread_mutex_lock (&task_mutex);
    raise (atoi (g_getenv ("E2_SMOKE_SIGNAL")));
    pthread_mutex_unlock (&task_mutex);
    path = g_build_filename (directory, "signal-returned", NULL);
    g_assert_true (g_file_set_contents (path, "unlocked", -1, NULL));
    g_free (path);
    return FALSE;
}
/* An extracted package must load its own plugins, not a different GTK build
 * already installed in /usr. This redirection exists only in the test helper. */
GModule *g_module_open (const gchar *filename, GModuleFlags flags)
{
    GModule *(*open_module) (const gchar *, GModuleFlags) = dlsym (RTLD_NEXT, "g_module_open");
    const gchar *root = g_getenv ("E2_SMOKE_PACKAGE_ROOT");
    if (root != NULL && filename != NULL && g_str_has_prefix (filename, "/usr/lib/emelfm2/plugins/"))
    {
        gchar *path = g_strconcat (root, filename, NULL);
        GModule *module = open_module (path, flags);
        g_free (path);
        return module;
    }
    return open_module (filename, flags);
}
static gboolean smoke (gpointer data)
{
    const gchar *directory = g_getenv ("E2_SMOKE_DIR");
    if (curr_view == NULL || curr_view->dir[0] == '\0' ||
        g_atomic_int_get (&curr_view->listcontrols.cd_working)) return TRUE;
    gchar *pane = g_build_filename (directory, "pane", NULL);
    gchar command[] = "sh -c 'printf native-command > native-started'";
    fprintf (stderr, "smoke: directory %s; command start\n", curr_view->dir);
    CLOSEBGL
    e2_command_run_at (command, pane, E2_COMMAND_RANGE_DEFAULT, app.main_window);
    OPENBGL
    fprintf (stderr, "smoke: command dispatched\n");
#ifdef E2_VTE
    if (!strcmp (g_getenv ("E2_SMOKE_VTE"), "1"))
    {
        gchar *shell = g_build_filename (directory, "shell", NULL);
        e2_option_str_set_direct (e2_option_get ("terminal-shell"), shell);
        g_free (shell);
        fprintf (stderr, "smoke: terminal action start\n");
        e2_action_run_simple_from ("terminal.open_here", NULL, app.main_window);
    }
#endif
    fprintf (stderr, "smoke: all dispatched\n");
    g_free (pane);
    if (g_getenv ("E2_SMOKE_SIGNAL") != NULL)
        g_timeout_add (10, signal_while_locked, NULL);
    return FALSE;
}
__attribute__((constructor)) static void schedule_smoke (void)
{ unsetenv ("LD_PRELOAD"); g_timeout_add (100, smoke, NULL); }
