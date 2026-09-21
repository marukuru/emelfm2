/* Real PTY and child-ownership regressions, using the production adapters and
 * native-command monitor. Run in Xvfb; all writes stay in a temporary directory. */
#include "../src/command/e2_terminal.c"
#include "../src/command/e2_command.c"
#include <glib/gstdio.h>
E2_MainData app;
pthread_mutex_t task_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Minimal application adapters; the session code and VTE widgets are real. */
void printd_raw (gint level, gchar *file, gint line, const gchar *format, ...) {}
static ViewInfo test_view;
ViewInfo *curr_view = &test_view;
pthread_mutex_t display_mutex = PTHREAD_MUTEX_INITIALIZER;
void e2_main_close_uilock (void) { pthread_mutex_lock (&display_mutex); }
void e2_main_open_uilock (void) { pthread_mutex_unlock (&display_mutex); }
gchar *(*e2_fname_to_locale) (const gchar *) = g_strdup;
void e2_utf8_fname_free (gchar *converted, const gchar *original)
{ if (converted != original) g_free (converted); }
gchar *e2_option_str_get (gchar *name)
{
    if (!strcmp (name, "terminal-shell")) return "/bin/bash";
    if (!strcmp (name, "terminal-font")) return "Monospace 10";
    if (!strcmp (name, "terminal-foreground")) return "#dddddd";
    return "#202020";
}
gint e2_option_int_get (gchar *name) { return 1000; }
gboolean e2_window_output_show (GtkWidget *widget, gpointer data) { return TRUE; }
GList *e2_fileview_get_selected_local (ViewInfo *view, gboolean updir) { return NULL; }
static gint dialog_response = GTK_RESPONSE_CANCEL;
static gboolean destroy_in_dialog;
gint e2_dialog_run_simple (GtkWidget *dialog, GtkWidget *parent)
{
    if (destroy_in_dialog) gtk_widget_destroy (current_session ()->page);
    return dialog_response;
}

typedef struct {
    GPid pid;
    gint spawned, exited, status;
    gboolean failed, known;
} Result;
static void spawned (GPid pid, const GError *error, gpointer data)
{
    Result *result = data;
    result->pid = pid;
    result->spawned++;
    result->failed = error != NULL;
}
static void exited (gint status, gboolean known, gpointer data)
{
    Result *result = data;
    result->exited++;
    result->status = status;
    result->known = known;
}
static void pump (void)
{
    while (g_main_context_iteration (NULL, FALSE));
    g_usleep (1000);
}
static void wait_result (Result *result, gboolean want_exit)
{
    gint64 until = g_get_monotonic_time () + 10000000;
    while ((!result->spawned || (want_exit && !result->exited)) &&
           g_get_monotonic_time () < until) pump ();
    g_assert_cmpint (result->spawned, ==, 1);
    if (want_exit) g_assert_cmpint (result->exited, ==, 1);
}
static GtkWidget *make_terminal (GtkWidget *window, Result *result)
{
    GtkWidget *terminal = e2_terminal_backend_new (exited, result);
    gtk_container_add (GTK_CONTAINER (window), terminal);
    e2_terminal_backend_configure (terminal, 1000, "Monospace 10", "#dddddd", "#202020");
    gtk_widget_show_all (window);
    return terminal;
}
static void test_quotes (void)
{
    const gchar *names[] = { "/tmp/a b", "/tmp/a'b", "/tmp/a\"b", "/tmp/a\\b",
        "/tmp/a\nb\n", "/tmp/*?[x]", "/tmp/-leading", "/tmp/Grüße 日本語", NULL };
    const gchar *shells[] = { "/bin/sh", "/bin/bash", "/bin/dash", "/usr/bin/zsh", "/usr/bin/fish", NULL };
    guint i, j;
    for (i = 0; shells[i]; i++)
    {
        if (!g_file_test (shells[i], G_FILE_TEST_IS_EXECUTABLE)) continue;
        for (j = 0; names[j]; j++)
        {
            gchar *quote = quote_path (names[j], shells[i]);
            gchar *command = g_strconcat ("printf '%s' ", quote, NULL);
            gchar *argv[] = { (gchar *)shells[i], "-c", command, NULL };
            gchar *output = NULL;
            gint status;
            g_assert_true (g_spawn_sync (NULL, argv, NULL, 0, NULL, NULL, &output, NULL, &status, NULL));
            g_assert_cmpint (status, ==, 0);
            g_assert_cmpstr (output, ==, names[j]);
            g_free (output); g_free (command); g_free (quote);
        }
    }
    g_assert_null (quote_path ("/tmp/a", "/bin/csh"));
}
static void test_ownership (void)
{
    enum { COUNT = 48 };
    E2_TaskRuntime tasks[COUNT] = {{0}};
    GPid pids[COUNT];
    Result result = {0};
    GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    GtkWidget *terminal = make_terminal (window, &result);
    GCancellable *cancel = g_cancellable_new ();
    gchar *argv[] = { "/bin/sh", "-c", "printf 'terminal output\\n'; exit 7", NULL };
    e2_terminal_backend_spawn (terminal, "/tmp", argv, cancel, spawned, &result);
    guint i;
    for (i = 0; i < COUNT; i++)
    {
        pids[i] = fork ();
        g_assert_cmpint (pids[i], >=, 0);
        if (!pids[i]) _exit (i % 8);
        tasks[i].pid = pids[i];
        tasks[i].status = E2_TASK_RUNNING;
        pthread_mutex_lock (&task_mutex);
        app.taskhistory = g_list_append (app.taskhistory, &tasks[i]);
        pthread_mutex_unlock (&task_mutex);
        _e2_command_monitor_child (pids[i]);
    }
    /* Neither initialization, status queries nor monitoring may reap this PID. */
    GPid foreign = fork ();
    if (!foreign) _exit (19);
    g_usleep (20000);
    e2_command_find_process (foreign);
    gint status;
    g_assert_cmpint (waitpid (foreign, &status, 0), ==, foreign);
    g_assert_cmpint (WEXITSTATUS (status), ==, 19);
    gchar *sync[] = { "/bin/sh", "-c", "exit 23", NULL };
    g_assert_true (g_spawn_sync (NULL, sync, NULL, 0, NULL, NULL, NULL, NULL, &status, NULL));
    g_assert_cmpint (WEXITSTATUS (status), ==, 23);
    wait_result (&result, TRUE);
    g_assert_false (result.failed);
#ifdef E2_VTE3
    g_assert_true (result.known);
    g_assert_cmpint (WEXITSTATUS (result.status), ==, 7);
#else
    g_assert_false (result.known);
#endif
    gint64 until = g_get_monotonic_time () + 10000000;
    for (i = 0; i < COUNT; i++)
    {
        while (tasks[i].status == E2_TASK_RUNNING && g_get_monotonic_time () < until) pump ();
        g_assert_cmpint (tasks[i].status, ==, E2_TASK_COMPLETED);
        g_assert_cmpint (tasks[i].ex.command.exit, ==, i % 8);
        g_assert_cmpint (waitpid (pids[i], NULL, WNOHANG), ==, -1);
        g_assert_cmpint (errno, ==, ECHILD);
    }
    g_assert_cmpint (waitpid (result.pid, NULL, WNOHANG), ==, -1);
    g_assert_cmpint (errno, ==, ECHILD);
    pthread_mutex_lock (&task_mutex);
    g_list_free (app.taskhistory); app.taskhistory = NULL;
    pthread_mutex_unlock (&task_mutex);
    gtk_widget_destroy (window);
    g_object_unref (cancel);
}
static void test_stop_continue (void)
{
    E2_TaskRuntime task = {0};
    GPid pid = fork ();
    if (!pid) { raise (SIGSTOP); _exit (13); }
    task.pid = pid; task.status = E2_TASK_RUNNING;
    app.taskhistory = g_list_append (NULL, &task);
    _e2_command_monitor_child (pid);
    gint64 until = g_get_monotonic_time () + 5000000;
    while (task.status != E2_TASK_PAUSED && g_get_monotonic_time () < until) pump ();
    g_assert_cmpint (task.status, ==, E2_TASK_PAUSED);
    kill (pid, SIGCONT);
    while (task.status != E2_TASK_COMPLETED && g_get_monotonic_time () < until) pump ();
    g_assert_cmpint (task.status, ==, E2_TASK_COMPLETED);
    g_assert_cmpint (task.ex.command.exit, ==, 13);
    pthread_mutex_lock (&task_mutex);
    g_list_free (app.taskhistory); app.taskhistory = NULL;
    pthread_mutex_unlock (&task_mutex);
}
static void test_failure_disposal (void)
{
    guint i;
    for (i = 0; i < 16; i++)
    {
        Result result = {0};
        GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        GtkWidget *terminal = make_terminal (window, &result);
        GCancellable *cancel = g_cancellable_new ();
        gchar *bad[] = { "/no/such/emelfm2-shell", "-i", NULL };
        gchar *quick[] = { "/bin/sh", "-c", "exit 0", NULL };
        e2_terminal_backend_spawn (terminal, "/tmp", i ? quick : bad, cancel, spawned, &result);
        if (i) { g_cancellable_cancel (cancel); gtk_widget_destroy (window); }
        wait_result (&result, FALSE);
        if (!i) { g_assert_true (result.failed); gtk_widget_destroy (window); }
        /* Give VTE its completion callback before reusing stack callback data. */
        gint64 until = g_get_monotonic_time () + 50000;
        while (g_get_monotonic_time () < until) pump ();
        g_object_unref (cancel);
    }
}
static void test_fullscreen (void)
{
    const gchar *programs[] = { "/usr/bin/top", "/usr/bin/htop", NULL };
    for (guint i = 0; programs[i]; i++)
    {
        if (!g_file_test (programs[i], G_FILE_TEST_IS_EXECUTABLE)) continue;
        Result result = {0};
        GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
        gtk_window_set_default_size (GTK_WINDOW (window), 900, 500);
        GtkWidget *terminal = make_terminal (window, &result);
        GCancellable *cancel = g_cancellable_new ();
        gchar *argv[] = { (gchar *)programs[i], NULL };
        e2_terminal_backend_spawn (terminal, "/tmp", argv, cancel, spawned, &result);
        wait_result (&result, FALSE);
        g_assert_false (result.failed);
        gint64 until = g_get_monotonic_time () + 5000000;
        gboolean found = FALSE;
        while (!found && g_get_monotonic_time () < until)
        {
            pump ();
            gchar *text = e2_terminal_backend_text (terminal);
            found = text != NULL && strstr (text, "PID") != NULL;
            g_free (text);
        }
        g_assert_true (found);
        gtk_window_resize (GTK_WINDOW (window), 1000, 600);
        for (guint j = 0; j < 200; j++) pump ();
        e2_terminal_backend_send (terminal, "q");
        wait_result (&result, TRUE);
        gtk_widget_destroy (window);
        g_object_unref (cancel);
    }
}
/* Session ownership/menus are exercised in run-vte-ui.sh. Keep this harness
 * focused on the real adapters, quoting, PTY job control and child ownership. */
static void test_interactive (void)
{
    gchar *directory = g_dir_make_tmp ("emelfm2-terminal-XXXXXX", NULL);
    gchar *resultfile = g_build_filename (directory, "result", NULL);
    gchar *original_cwd = g_get_current_dir ();
    Result result = {0};
    GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    GtkWidget *terminal = make_terminal (window, &result);
    GCancellable *cancel = g_cancellable_new ();
    gchar *args[] = { "/bin/bash", "--noprofile", "--norc", "-i", NULL };
    e2_terminal_backend_spawn (terminal, directory, args, cancel, spawned, &result);
    wait_result (&result, FALSE);
    g_assert_cmpint (result.pid, >, 0);
    gint64 until;
    gchar *cwd = g_get_current_dir ();
    g_assert_cmpstr (cwd, ==, original_cwd);
    g_free (cwd); g_free (original_cwd);
    /* Interactive quoting must preserve newlines, and insert must not submit. */
    const gchar *name = "/tmp/space '\" *? [x] 日本語\nend\n";
    gchar *quoted = quote_path (name, "/bin/bash");
    e2_terminal_backend_send (terminal, "stty -echo; unset HISTFILE; bind 'set enable-bracketed-paste off'\n");
    for (guint i = 0; i < 100; i++) pump ();
    e2_terminal_backend_send (terminal, "printf '%s' ");
    e2_terminal_backend_insert (terminal, quoted);
    for (guint i = 0; i < 100; i++) pump ();
    g_assert_false (g_file_test (resultfile, G_FILE_TEST_EXISTS));
    e2_terminal_backend_send (terminal, " >result\n");
    until = g_get_monotonic_time () + 5000000;
    while (!g_file_test (resultfile, G_FILE_TEST_EXISTS) && g_get_monotonic_time () < until) pump ();
    gchar *content = NULL;
    g_assert_true (g_file_get_contents (resultfile, &content, NULL, NULL));
    g_assert_cmpstr (content, ==, name);
    g_free (content); g_free (quoted);
    /* Real PTY job control: stop a foreground job, resume it, interrupt it. */
    e2_terminal_backend_send (terminal, "sleep 30\n");
    for (guint i = 0; i < 100; i++) pump ();
    e2_terminal_backend_send (terminal, "\032");
    for (guint i = 0; i < 100; i++) pump ();
    e2_terminal_backend_send (terminal, "jobs >jobs; fg\n");
    for (guint i = 0; i < 100; i++) pump ();
    e2_terminal_backend_send (terminal, "\003");
    for (guint i = 0; i < 100; i++) pump ();
    gchar *jobs = g_build_filename (directory, "jobs", NULL);
    g_assert_true (g_file_get_contents (jobs, &content, NULL, NULL));
    g_assert_nonnull (strstr (content, "Stopped"));
    g_free (content); g_remove (jobs); g_free (jobs);
    e2_terminal_backend_send (terminal, "exit 11\n");
    until = g_get_monotonic_time () + 5000000;
    while (!result.exited && g_get_monotonic_time () < until) pump ();
    g_assert_true (result.exited);
    gtk_widget_destroy (window);
    g_object_unref (cancel);
    g_remove (resultfile); g_rmdir (directory);
    g_free (resultfile); g_free (directory);
}

int main (int argc, char **argv)
{
    gtk_init (&argc, &argv);
    test_quotes ();
    test_ownership ();
    test_stop_continue ();
    test_failure_disposal ();
    test_interactive ();
    test_fullscreen ();
    g_print ("terminal: quoting, child ownership, job control, lifecycle, top/htop and resize passed\n");
    return 0;
}
