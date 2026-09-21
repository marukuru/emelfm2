/* Real PTY title/foreground-context checks, without changing shell dotfiles. */
#include "emelfm2.h"
#include "e2_terminal_backend.h"
#include "e2_terminal_context.h"
#include "e2_terminal_shell.h"
#include <sys/stat.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <signal.h>

static GPid shell_pid;
static gboolean shell_exited;
static void spawned (GPid pid, const GError *error, gpointer data)
{
    g_assert_null (error);
    shell_pid = pid;
}
static void exited (gint status, gboolean known, gpointer data)
{ shell_exited = TRUE; }
static void pump (void)
{
    while (g_main_context_iteration (NULL, FALSE));
    g_usleep (10000);
}
static void wait_label (GtkWidget *terminal, E2_TerminalContext *context, const gchar *expected)
{
    gint64 deadline = g_get_monotonic_time () + 5000000;
    gchar *actual = NULL;
    do
    {
        pump ();
        GPid foreground = e2_terminal_backend_foreground_pid (terminal);
        if (foreground <= 0) continue;
        e2_terminal_context_update (context, foreground,
            e2_terminal_backend_title (terminal), e2_terminal_backend_directory_uri (terminal));
        g_free (actual);
        actual = e2_terminal_context_label (context);
        if (!strcmp (actual, expected)) break;
    } while (g_get_monotonic_time () < deadline);
    g_assert_cmpstr (actual, ==, expected);
    g_free (actual);
}
static void change_directory (GtkWidget *terminal, const gchar *directory)
{
    gchar *quoted = g_shell_quote (directory);
    gchar *command = g_strconcat ("cd -- ", quoted, "\n", NULL);
    e2_terminal_backend_send (terminal, command);
    g_free (command); g_free (quoted);
}
static void wait_text (GtkWidget *terminal, const gchar *needle)
{
    gint64 deadline = g_get_monotonic_time () + 5000000;
    gboolean found = FALSE;
    do
    {
        pump ();
        gchar *text = e2_terminal_backend_text (terminal);
        found = text != NULL && strstr (text, needle) != NULL;
        g_free (text);
    } while (!found && g_get_monotonic_time () < deadline);
    g_assert_true (found);
}
static void wait_jobs (GtkWidget *terminal, gboolean expected)
{
    gint64 deadline = g_get_monotonic_time () + 5000000;
    gboolean busy;
    do
    {
        pump ();
        busy = e2_terminal_context_has_jobs (shell_pid,
            e2_terminal_backend_foreground_pid (terminal), "/bin/bash");
    } while (busy != expected && g_get_monotonic_time () < deadline);
    g_assert_cmpint (busy, ==, expected);
}
static void test_jobs (const gchar *root)
{
    GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    GtkWidget *terminal = e2_terminal_backend_new (exited, NULL);
    gtk_container_add (GTK_CONTAINER (window), terminal);
    gtk_widget_show_all (window);
    GCancellable *cancel = g_cancellable_new ();
    gchar *args[] = { "/bin/bash", "--noprofile", "--norc", "-i", NULL };
    shell_exited = FALSE;
    shell_pid = 0;
    e2_terminal_backend_spawn (terminal, root, args, cancel, spawned, NULL);
    gint64 deadline = g_get_monotonic_time () + 5000000;
    while (shell_pid <= 0 && g_get_monotonic_time () < deadline) pump ();
    g_assert_cmpint (shell_pid, >, 0);
    e2_terminal_backend_send (terminal, "unset HISTFILE; stty -echo; PS1='IDLE-PROMPT> '\n");
    wait_text (terminal, "IDLE-PROMPT> ");
    wait_jobs (terminal, FALSE);
    e2_terminal_backend_send (terminal, "sleep 60\n");
    wait_jobs (terminal, TRUE);
    e2_terminal_backend_send (terminal, "\032");
    wait_text (terminal, "Stopped");
    wait_jobs (terminal, TRUE);
    e2_terminal_backend_send (terminal, "bg; printf 'BACKGROUND-READY\\n'\n");
    wait_text (terminal, "BACKGROUND-READY");
    g_assert_cmpint (e2_terminal_backend_foreground_pid (terminal), ==, shell_pid);
    wait_jobs (terminal, TRUE);
    e2_terminal_backend_send (terminal, "kill %1; wait; printf 'JOBS-FINISHED\\n'\n");
    wait_text (terminal, "JOBS-FINISHED");
    wait_jobs (terminal, FALSE);
    e2_terminal_backend_send (terminal, "exec sleep 60\n");
    wait_jobs (terminal, TRUE);
    g_assert_cmpint (e2_terminal_backend_foreground_pid (terminal), ==, shell_pid);
    kill (shell_pid, SIGTERM);
    deadline = g_get_monotonic_time () + 5000000;
    while (!shell_exited && g_get_monotonic_time () < deadline) pump ();
    g_assert_true (shell_exited);
    g_assert_false (e2_terminal_context_has_jobs (shell_pid, -1, "/bin/bash"));
    gtk_widget_destroy (window);
    g_object_unref (cancel);
}
static void test_metadata (const gchar *root, const gchar *folder, gboolean array)
{
    gchar *rc = g_build_filename (root, "user rc", NULL);
    const gchar *original = array ? "unset HISTFILE\nPS1='UNCHANGED-PROMPT> '\nPROMPT_COMMAND=('printf \"FIRST-HOOK-%s\\n\" \"$?\"' 'printf \"SECOND-HOOK\\n\"')\n" :
        "unset HISTFILE\nPS1='UNCHANGED-PROMPT> '\nPROMPT_COMMAND='printf \"FIRST-HOOK-%s\\n\" \"$?\"; printf \"SECOND-HOOK\\n\"'\n";
    g_assert_true (g_file_set_contents (rc, original, -1, NULL));
    GError *error = NULL;
    gchar *hook = e2_terminal_shell_rc (rc, &error);
    g_assert_no_error (error);
    struct stat st;
    g_assert_cmpint (g_stat (hook, &st), ==, 0);
    g_assert_cmpint (st.st_mode & 0777, ==, 0600);
    GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    GtkWidget *terminal = e2_terminal_backend_new (exited, NULL);
    gtk_container_add (GTK_CONTAINER (window), terminal);
    gtk_widget_show_all (window);
    GCancellable *cancel = g_cancellable_new ();
    gchar *args[] = { "/bin/bash", "--noprofile", "--rcfile", hook, "-i", NULL };
    shell_exited = FALSE;
    e2_terminal_backend_spawn (terminal, root, args, cancel, spawned, NULL);
    wait_text (terminal, "UNCHANGED-PROMPT> ");
    wait_text (terminal, "SECOND-HOOK");
    E2_TerminalContext *context = e2_terminal_context_new (root);
    change_directory (terminal, folder);
    gchar *expected = g_strconcat (g_get_user_name (), "@日本語 folder", NULL);
    wait_label (terminal, context, expected);
    /* Require actual shell reports, not just /proc fallback. */
    gint64 deadline = g_get_monotonic_time () + 5000000;
    while ((e2_terminal_backend_title (terminal) == NULL
        || strstr (e2_terminal_backend_title (terminal), "日本語 folder") == NULL)
        && g_get_monotonic_time () < deadline) pump ();
    g_assert_nonnull (strstr (e2_terminal_backend_title (terminal), "日本語 folder"));
#ifdef E2_VTE3
    const gchar *uri = e2_terminal_backend_directory_uri (terminal);
    gchar *decoded = g_filename_from_uri (uri, NULL, NULL);
    g_assert_cmpstr (decoded, ==, folder);
    g_free (decoded);
#endif
    e2_terminal_backend_send (terminal, "stty -echo; false\n");
    wait_text (terminal, "FIRST-HOOK-1");
    e2_terminal_backend_send (terminal, "printf 'Needle.*日本語\\nOther line\\nNeedle.*日本語\\n'\n");
    wait_text (terminal, "Other line");
    g_assert_true (e2_terminal_backend_search (terminal, "needle.*日本語", FALSE));
    g_assert_true (e2_terminal_backend_has_selection (terminal));
    g_assert_true (e2_terminal_backend_search (terminal, "needle.*日本語", FALSE));
    g_assert_true (e2_terminal_backend_search (terminal, "needle.*日本語", FALSE));
    g_assert_true (e2_terminal_backend_search (terminal, "needle.*日本語", TRUE));
    g_assert_false (e2_terminal_backend_search (terminal, "NeedleZZ日本語", FALSE));
    g_assert_false (e2_terminal_backend_search (terminal, "", FALSE));
    gchar *unchanged;
    g_assert_true (g_file_get_contents (rc, &unchanged, NULL, NULL));
    g_assert_cmpstr (unchanged, ==, original);
    g_free (unchanged);
    e2_terminal_backend_send (terminal, "exit\n");
    deadline = g_get_monotonic_time () + 5000000;
    while (!shell_exited && g_get_monotonic_time () < deadline) pump ();
    g_assert_true (shell_exited);
    gtk_widget_destroy (window);
    g_object_unref (cancel);
    e2_terminal_context_free (context);
    g_unlink (hook); g_unlink (rc);
    g_free (hook); g_free (rc); g_free (expected);
}
int main (int argc, char **argv)
{
    gtk_init (&argc, &argv);
    gchar *root = g_dir_make_tmp ("emelfm2-title-XXXXXX", NULL);
    gchar *folder = g_build_filename (root, "日本語 folder", NULL);
    gchar *renamed = g_build_filename (root, "renamed", NULL);
    gchar *controls = g_build_filename (root, "line\nbreak", NULL);
    g_assert_cmpint (g_mkdir (folder, 0700), ==, 0);
    g_assert_cmpint (g_mkdir (controls, 0700), ==, 0);
    E2_TerminalContext *context = e2_terminal_context_new (root);
    gchar *initial = e2_terminal_context_label (context);
    gchar *at = strrchr (initial, '@');
    gchar *user = g_strndup (initial, at - initial);
    GtkWidget *window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    GtkWidget *terminal = e2_terminal_backend_new (exited, NULL);
    gtk_container_add (GTK_CONTAINER (window), terminal);
    gtk_widget_show_all (window);
    GCancellable *cancel = g_cancellable_new ();
    gchar *args[] = { "/bin/bash", "--noprofile", "--norc", "-i", NULL };
    e2_terminal_backend_spawn (terminal, root, args, cancel, spawned, NULL);
    wait_label (terminal, context, initial);
    g_assert_cmpint (shell_pid, >, 0);
    e2_terminal_backend_send (terminal, "unset HISTFILE; PROMPT_COMMAND=; PS1=\n");
    change_directory (terminal, folder);
    gchar *expected = g_strconcat (user, "@日本語 folder", NULL);
    wait_label (terminal, context, expected);
    g_free (expected);
    gchar *local = e2_terminal_context_local_directory (context);
    g_assert_cmpstr (local, ==, folder);
    g_free (local);
    g_assert_cmpint (g_rename (folder, renamed), ==, 0);
    expected = g_strconcat (user, "@renamed", NULL);
    wait_label (terminal, context, expected);

    e2_terminal_backend_send (terminal, "bash --noprofile --norc -i\n");
    gint64 deadline = g_get_monotonic_time () + 5000000;
    while (e2_terminal_backend_foreground_pid (terminal) == shell_pid
        && g_get_monotonic_time () < deadline) pump ();
    g_assert_cmpint (e2_terminal_backend_foreground_pid (terminal), !=, shell_pid);
    change_directory (terminal, "/");
    gchar *root_label = g_strconcat (user, "@/", NULL);
    wait_label (terminal, context, root_label);
    /* Use real terminal escape sequences to simulate a switched/remote user. */
    e2_terminal_backend_send (terminal, "printf '\\033]0;admin@remote:/var/lib/administration\\007'\n");
    wait_label (terminal, context, "admin@administration");
    g_assert_null (e2_terminal_context_local_directory (context));
    gchar *description = e2_terminal_context_description (context);
    g_assert_cmpstr (description, ==, "remote:/var/lib/administration");
    g_free (description);
    /* Even an existing local path must not be navigable when reported remote. */
    e2_terminal_backend_send (terminal, "printf '\\033]0;admin@remote:/tmp\\007'\n");
    wait_label (terminal, context, "admin@tmp");
    g_assert_null (e2_terminal_context_local_directory (context));
#ifdef E2_VTE3
    e2_terminal_backend_send (terminal, "printf '\\033]7;file://remote/var/data/space%%20name\\007'\n");
    wait_label (terminal, context, "admin@space name");
#endif
    e2_terminal_backend_send (terminal, "unset HISTFILE; exit\n");
    wait_label (terminal, context, expected);
    /* script creates an inner PTY, like sudo configured with use_pty. */
    e2_terminal_backend_send (terminal, "script -q -c 'bash --noprofile --norc -i' /dev/null\n");
    deadline = g_get_monotonic_time () + 5000000;
    while (e2_terminal_backend_foreground_pid (terminal) == shell_pid
        && g_get_monotonic_time () < deadline) pump ();
    g_assert_cmpint (e2_terminal_backend_foreground_pid (terminal), !=, shell_pid);
    change_directory (terminal, "/");
    wait_label (terminal, context, root_label);
    e2_terminal_backend_send (terminal, "unset HISTFILE; exit\n");
    deadline = g_get_monotonic_time () + 5000000;
    while (e2_terminal_backend_foreground_pid (terminal) != shell_pid
        && g_get_monotonic_time () < deadline) pump ();
    g_assert_cmpint (e2_terminal_backend_foreground_pid (terminal), ==, shell_pid);
    wait_label (terminal, context, expected);
    g_free (expected);
    /* A stale shell title must not pin the directory after a local cd. */
    e2_terminal_backend_send (terminal, "printf '\\033]0;guest@remote:/opt/guest-folder\\007'\n");
    wait_label (terminal, context, "guest@guest-folder");
    change_directory (terminal, controls);
    expected = g_strconcat (user, "@line?break", NULL);
    wait_label (terminal, context, expected);
    g_free (expected);
    change_directory (terminal, "/");
    wait_label (terminal, context, root_label);
    e2_terminal_backend_send (terminal, "exit\n");
    deadline = g_get_monotonic_time () + 5000000;
    while (!shell_exited && g_get_monotonic_time () < deadline) pump ();
    g_assert_true (shell_exited);
    gtk_widget_destroy (window);
    g_object_unref (cancel);
    e2_terminal_context_free (context);
    g_assert_cmpint (g_mkdir (folder, 0700), ==, 0);
    test_metadata (root, folder, TRUE);
    test_metadata (root, folder, FALSE);
    test_jobs (root);
    g_rmdir (folder);
    g_rmdir (controls); g_rmdir (renamed); g_rmdir (root);
    g_free (root_label); g_free (initial); g_free (user);
    g_free (controls); g_free (renamed); g_free (folder); g_free (root);
    g_print ("terminal titles: cwd, rename, nested shells, reported users, Unicode, root, stale metadata, wrapped literal search and opt-in Bash hooks passed\n");
    return 0;
}
