/* Optional terminal sessions. SPDX-License-Identifier: GPL-3.0-or-later */
#include "e2_terminal.h"
#ifdef E2_VTE
#include "e2_terminal_backend.h"
#include "e2_action.h"
#include "e2_option.h"
#include "e2_fileview.h"
#include "e2_window.h"
#include "e2_dialog.h"
#include "e2_icons.h"
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

/* This notebook is deliberately separate from app.outbook and app.tabslist.
 * Application output, task retabbing and detached log views remain text-only. */
typedef struct
{
    gint refs;
    GtkWidget *page, *terminal, *label, *status;
    gchar *directory, *shell;
    GCancellable *cancel;
    GPid pid;
    gboolean pending, disposed, exited;
    guint number;
} TerminalSession;
static GtkWidget *terminal_book;
static GList *sessions;
static gboolean dispatch_action (gpointer data);
static guint next_number;
static pthread_t ui_thread;

static void session_unref (TerminalSession *session)
{
    if (--session->refs != 0) return;
    g_object_unref (session->cancel);
    g_free (session->directory);
    g_free (session->shell);
    g_free (session);
}
static void session_label (TerminalSession *session, const gchar *state)
{
    gchar *text = g_strdup_printf (_("Terminal %u — %s"), session->number, state);
    gtk_label_set_text (GTK_LABEL (session->label), text);
    g_free (text);
}
static void session_exited (gint status, gboolean known, gpointer data)
{
    TerminalSession *session = data;
    if (session->disposed) return;
    session->pid = 0;
    session->exited = TRUE;
    session_label (session, _("exited"));
    gchar *text;
    if (known && WIFEXITED (status))
        text = g_strdup_printf (_("Shell exited with status %d. Use Restart to start another shell."), WEXITSTATUS (status));
    else if (known && WIFSIGNALED (status))
        text = g_strdup_printf (_("Shell terminated by signal %d."), WTERMSIG (status));
    else
        text = g_strdup (_("Shell exited (exit status unavailable). Use Restart to start another shell."));
    gtk_label_set_text (GTK_LABEL (session->status), text);
    g_free (text);
}
static void session_spawned (GPid pid, const GError *error, gpointer data)
{
    TerminalSession *session = data;
    session->pending = FALSE;
    if (!session->disposed)
    {
        if (error != NULL)
        {
            session_label (session, _("failed"));
            gtk_label_set_text (GTK_LABEL (session->status), error->message);
        }
        else if (!session->exited)
        {
            session->pid = pid;
            session_label (session, _("running"));
            gtk_label_set_text (GTK_LABEL (session->status), session->directory);
        }
    }
    /* On destruction VTE owns cancellation, hangup and child reaping. A PID
     * returned after disposal must not be signalled: it may already be reaped. */
    session_unref (session);
}
static void session_destroyed (GtkWidget *widget, gpointer data)
{
    TerminalSession *session = data;
    session->disposed = TRUE;
    g_cancellable_cancel (session->cancel);
    sessions = g_list_remove (sessions, session);
    /* Destroying the PTY sends a hangup to its foreground process group.
     * VTE retains the child watch; we never waitpid() a terminal child. */
    if (session->pid > 0) kill (session->pid, SIGHUP);
    session->pid = 0;
    /* The widget may stay referenced by an async spawn. Suppress exit callbacks
     * before releasing the session's widget ownership. */
    g_signal_handlers_disconnect_matched (session->terminal, G_SIGNAL_MATCH_ID,
        g_signal_lookup ("child-exited", G_OBJECT_TYPE (session->terminal)),
        0, NULL, NULL, NULL);
    session_unref (session);
}
static TerminalSession *current_session (void)
{
    if (terminal_book == NULL) return NULL;
    GtkWidget *page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (terminal_book),
        gtk_notebook_get_current_page (GTK_NOTEBOOK (terminal_book)));
    return page == NULL ? NULL : g_object_get_data (G_OBJECT (page), "e2-terminal-session");
}
gboolean e2_terminal_has_focus (void)
{
    if (app.main_window == NULL) return FALSE;
    GtkWidget *focus = gtk_window_get_focus (GTK_WINDOW (app.main_window));
    GList *member;
    for (member = sessions; member != NULL; member = member->next)
        if (((TerminalSession *)member->data)->terminal == focus) return TRUE;
    return FALSE;
}
/* GtkWindow normally activates accelerators before propagating to its child.
 * Route focused terminal keys first, so application accelerators cannot consume
 * job-control or interactive-application keys. */
gboolean e2_terminal_window_key (GtkWidget *window, GdkEventKey *event, gpointer data)
{
    if (!e2_terminal_has_focus ()) return FALSE;
    gtk_window_propagate_key_event (GTK_WINDOW (window), event);
    return TRUE;
}
static gboolean terminal_key (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
    guint mods = event->state & gtk_accelerator_get_default_mod_mask ();
    if (mods == (GDK_CONTROL_MASK | GDK_SHIFT_MASK))
    {
        switch (gdk_keyval_to_lower (event->keyval))
        {
            case GDK_F6: gtk_widget_grab_focus (curr_view->treeview); return TRUE;
            case GDK_c: e2_terminal_backend_copy (widget); return TRUE;
            case GDK_v: e2_terminal_backend_paste (widget); return TRUE;
        }
    }
    return FALSE; /* VTE gets Ctrl+C, Ctrl+Z, Tab, function keys, etc. */
}
static gint run_dialog (GtkWidget *dialog)
{
    /* Actions enter with the UI lock held; toolbar callbacks may not. The
     * application's dialog runner releases it while dispatching GTK events. */
    CLOSEBGL_IF_OPEN
    gint response = e2_dialog_run_simple (dialog, app.main_window);
    OPENBGL_IF_CLOSED
    return response;
}
static gboolean confirm_close (const gchar *message)
{
    GtkWidget *dialog = gtk_message_dialog_new (GTK_WINDOW (app.main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_WARNING,
        GTK_BUTTONS_NONE, "%s", message);
    gtk_dialog_add_buttons (GTK_DIALOG (dialog), _("Cancel"), GTK_RESPONSE_CANCEL,
        _("Close terminals"), GTK_RESPONSE_ACCEPT, NULL);
    gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_CANCEL);
    gint response = run_dialog (dialog);
    gtk_widget_destroy (dialog);
    return response == GTK_RESPONSE_ACCEPT;
}
static gboolean session_can_close (TerminalSession *session)
{
    return (!session->pending && session->pid <= 0) || confirm_close (
        _("This terminal has a running or starting shell. Closing it hangs up the shell and its terminal jobs. Close it?"));
}
static gboolean close_session (TerminalSession *session)
{
    session->refs++; /* The confirmation dialog can dispatch other actions. */
    gboolean close = session_can_close (session) && !session->disposed;
    if (close) gtk_widget_destroy (session->page);
    session_unref (session);
    return close;
}
static void close_session_clicked (GtkButton *button, TerminalSession *session)
{
    close_session (session);
}
static gboolean local_directory (void)
{
#ifdef E2_VFS
    if (curr_view->spacedata != NULL) return FALSE;
#endif
    gchar *directory = F_FILENAME_TO_LOCALE (curr_view->dir);
    gboolean local = g_path_is_absolute (directory) && g_file_test (directory, G_FILE_TEST_IS_DIR);
    F_FREE (directory, curr_view->dir);
    return local;
}
static void terminal_error (const gchar *message)
{
    GtkWidget *dialog = gtk_message_dialog_new (GTK_WINDOW (app.main_window),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
        GTK_BUTTONS_CLOSE, "%s", message);
    run_dialog (dialog);
    gtk_widget_destroy (dialog);
}
static void reveal_terminal (void)
{
    e2_window_output_show (NULL, NULL);
    /* The default log pane is very short. Give a newly focused terminal room
     * for interactive programs without changing the default startup layout. */
    if (app.window.output_paned != NULL)
    {
        GtkAllocation allocation;
        gtk_widget_get_allocation (app.window.output_paned, &allocation);
        gint wanted = MIN (300, allocation.height * 3 / 4);
        GtkPaned *paned = GTK_PANED (app.window.output_paned);
        if (allocation.height - gtk_paned_get_position (paned) < wanted)
            gtk_paned_set_position (paned, allocation.height - wanted);
    }
}
static void open_session (const gchar *directory)
{
    TerminalSession *session = g_new0 (TerminalSession, 1);
    session->refs = 1;
    session->number = ++next_number;
    session->directory = g_strdup (directory);
    const gchar *shell = e2_option_str_get ("terminal-shell");
    if (shell == NULL || *shell == '\0') shell = g_getenv ("SHELL");
    if (shell == NULL || *shell == '\0') shell = "/bin/sh";
    session->shell = g_strdup (shell);
    session->cancel = g_cancellable_new ();
    session->page = gtk_vbox_new (FALSE, 0);
    session->label = gtk_label_new ("");
    session->status = gtk_label_new (_("Starting shell…"));
    gtk_label_set_ellipsize (GTK_LABEL (session->status), PANGO_ELLIPSIZE_MIDDLE);
    GtkWidget *row = gtk_hbox_new (FALSE, 0);
    session->terminal = e2_terminal_backend_new (session_exited, session);
    e2_terminal_backend_configure (session->terminal,
        e2_option_int_get ("terminal-scrollback"), e2_option_str_get ("terminal-font"),
        e2_option_str_get ("terminal-foreground"), e2_option_str_get ("terminal-background"));
    gtk_box_pack_start (GTK_BOX (row), session->terminal, TRUE, TRUE, 0);
    GtkWidget *scrollbar = gtk_vscrollbar_new (e2_terminal_backend_adjustment (session->terminal));
    gtk_box_pack_start (GTK_BOX (row), scrollbar, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (session->page), row, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (session->page), session->status, FALSE, FALSE, 0);
    g_object_set_data (G_OBJECT (session->page), "e2-terminal-session", session);
    g_signal_connect (session->page, "destroy", G_CALLBACK (session_destroyed), session);
    g_signal_connect (session->terminal, "key-press-event", G_CALLBACK (terminal_key), session);
    sessions = g_list_append (sessions, session);
    session_label (session, _("starting"));
    GtkWidget *title = gtk_hbox_new (FALSE, 4);
    GtkWidget *close = e2_button_get_full (NULL, STOCK_NAME_CLOSE, GTK_ICON_SIZE_MENU,
        _("Close this terminal"), close_session_clicked, session, E2_BUTTON_SHOW_MISSING_ICON);
    gtk_widget_set_name (close, "terminal-close");
    atk_object_set_name (gtk_widget_get_accessible (close), _("Close terminal"));
    gtk_button_set_relief (GTK_BUTTON (close), GTK_RELIEF_NONE);
    gtk_box_pack_start (GTK_BOX (title), session->label, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (title), close, FALSE, FALSE, 0);
    gtk_widget_show_all (title);
    gint page = gtk_notebook_append_page (GTK_NOTEBOOK (terminal_book), session->page, title);
    gtk_widget_show_all (session->page);
    gtk_notebook_set_current_page (GTK_NOTEBOOK (terminal_book), page);
    reveal_terminal ();
    gtk_widget_grab_focus (session->terminal);
    gchar *argv[] = { session->shell, "-i", NULL };
    session->pending = TRUE;
    session->refs++; /* async callback owns this even after the page is closed */
    e2_terminal_backend_spawn (session->terminal, session->directory, argv,
        session->cancel, session_spawned, session);
}
static gboolean open_here (gpointer from, E2_ActionRuntime *art)
{
    if (!pthread_equal (pthread_self (), ui_thread))
    {
        g_idle_add (dispatch_action, GUINT_TO_POINTER (0));
        return TRUE;
    }
    if (!local_directory ())
    {
        terminal_error (_("A terminal requires an existing local directory. Select a mounted local pane first."));
        return FALSE;
    }
    gchar *local = F_FILENAME_TO_LOCALE (curr_view->dir);
    open_session (local);
    F_FREE (local, curr_view->dir);
    return TRUE;
}
static gboolean focus_terminal (gpointer from, E2_ActionRuntime *art)
{
    if (!pthread_equal (pthread_self (), ui_thread))
    {
        g_idle_add (dispatch_action, GUINT_TO_POINTER (1));
        return TRUE;
    }
    TerminalSession *session = current_session ();
    if (session == NULL && sessions != NULL) session = g_list_last (sessions)->data;
    if (session == NULL) return open_here (from, art);
    gtk_notebook_set_current_page (GTK_NOTEBOOK (terminal_book),
        gtk_notebook_page_num (GTK_NOTEBOOK (terminal_book), session->page));
    reveal_terminal ();
    gtk_widget_grab_focus (session->terminal);
    return TRUE;
}
static gboolean close_terminal (gpointer from, E2_ActionRuntime *art)
{
    if (!pthread_equal (pthread_self (), ui_thread))
    {
        g_idle_add (dispatch_action, GUINT_TO_POINTER (2));
        return TRUE;
    }
    TerminalSession *session = current_session ();
    if (session == NULL) return FALSE;
    return close_session (session);
}
static gboolean restart_terminal (gpointer from, E2_ActionRuntime *art)
{
    if (!pthread_equal (pthread_self (), ui_thread))
    {
        g_idle_add (dispatch_action, GUINT_TO_POINTER (3));
        return TRUE;
    }
    TerminalSession *session = current_session ();
    if (session == NULL) return FALSE;
    session->refs++;
    gboolean restart = session_can_close (session) && !session->disposed;
    if (restart)
    {
        gchar *directory = g_strdup (session->directory);
        gtk_widget_destroy (session->page);
        open_session (directory);
        g_free (directory);
    }
    session_unref (session);
    return restart;
}
/* Shell syntax is confined to deliberate generated-path insertion. Terminal
 * typing never enters the emelFM2 command dispatcher. */
static gchar *quote_path (const gchar *path, const gchar *shell)
{
    gchar *base = g_path_get_basename (shell);
    gboolean fish = !strcmp (base, "fish");
    gboolean posix = !strcmp (base, "sh") || !strcmp (base, "bash") ||
        !strcmp (base, "dash") || !strcmp (base, "zsh") ||
        !strcmp (base, "ksh") || !strcmp (base, "mksh");
    g_free (base);
    if (!fish && !posix) return NULL;
    if (!fish) return g_shell_quote (path);
    GString *quoted = g_string_new ("'");
    for (; *path; path++)
    {
        if (*path == '\\' || *path == '\'') g_string_append_c (quoted, '\\');
        g_string_append_c (quoted, *path);
    }
    g_string_append_c (quoted, '\'');
    return g_string_free (quoted, FALSE);
}
static gboolean insert_paths (gpointer from, E2_ActionRuntime *art)
{
    if (!pthread_equal (pthread_self (), ui_thread))
    {
        g_idle_add (dispatch_action, GUINT_TO_POINTER (4));
        return TRUE;
    }
    TerminalSession *session = current_session ();
    if (session == NULL || session->pid <= 0 || !local_directory ()) return FALSE;
    GList *selected = e2_fileview_get_selected_local (curr_view, FALSE), *member;
    GString *text = g_string_new (" ");
    gchar *directory = F_FILENAME_TO_LOCALE (curr_view->dir);
    gboolean valid = TRUE;
    for (member = selected; member != NULL; member = member->next)
    {
        FileInfo *info = member->data;
        gchar *path = g_build_filename (directory, info->filename, NULL);
        gchar *quoted = quote_path (path, session->shell);
        g_free (path);
        if (quoted == NULL) { valid = FALSE; break; }
        g_string_append (text, quoted);
        g_string_append_c (text, ' ');
        g_free (quoted);
    }
    F_FREE (directory, curr_view->dir);
    g_list_free (selected);
    if (valid)
    {
        e2_terminal_backend_insert (session->terminal, text->str);
        gtk_widget_grab_focus (session->terminal);
    }
    else terminal_error (_("Path insertion supports sh, bash, dash, zsh, ksh, mksh and fish. Select one in terminal preferences."));
    g_string_free (text, TRUE);
    return valid;
}
static void button_action (GtkWidget *button, gpointer data)
{
    guint action = GPOINTER_TO_UINT (data);
    TerminalSession *session = current_session ();
    switch (action)
    {
        case 0: open_here (NULL, NULL); break;
        case 1: focus_terminal (NULL, NULL); break;
        case 2: close_terminal (NULL, NULL); break;
        case 3: restart_terminal (NULL, NULL); break;
        case 4: insert_paths (NULL, NULL); break;
        case 5: if (session) e2_terminal_backend_copy (session->terminal); break;
        case 6: if (session) e2_terminal_backend_paste (session->terminal); break;
    }
}
static gboolean dispatch_action (gpointer data)
{
    if (terminal_book != NULL) button_action (NULL, data);
    return FALSE;
}
GtkWidget *e2_terminal_wrap_output (GtkWidget *output)
{
    ui_thread = pthread_self ();
    GtkWidget *box = gtk_vbox_new (FALSE, 0);
    GtkWidget *buttons = gtk_hbox_new (FALSE, 2);
    gtk_widget_set_name (buttons, "terminal-actions");
    const struct { guint action; const gchar *label, *icon, *tip; } actions[] = {
        { 0, N_("Open terminal here"), STOCK_NAME_EXECUTE,
            N_("Open an interactive terminal in this pane's current directory") },
        { 1, N_("Focus terminal"), STOCK_NAME_JUMP_TO,
            N_("Focus the terminal; Ctrl+Shift+F6 returns to the file list") },
        { 3, N_("Restart"), STOCK_NAME_REFRESH,
            N_("Restart the selected terminal in its original directory") },
        { 4, N_("Insert selected paths"), STOCK_NAME_ADD,
            N_("Insert the selected files' quoted paths into the terminal") },
        { 5, N_("Copy"), STOCK_NAME_COPY, N_("Copy selected terminal text (Ctrl+Shift+C)") },
        { 6, N_("Paste"), STOCK_NAME_PASTE, N_("Paste clipboard text into the terminal (Ctrl+Shift+V)") }
    };
    guint i;
    for (i = 0; i < G_N_ELEMENTS (actions); i++)
    {
        GtkWidget *button = e2_button_get_full (NULL, actions[i].icon, GTK_ICON_SIZE_MENU,
            _(actions[i].tip), button_action, GUINT_TO_POINTER (actions[i].action), E2_BUTTON_SHOW_MISSING_ICON);
        atk_object_set_name (gtk_widget_get_accessible (button), _(actions[i].label));
        gtk_box_pack_start (GTK_BOX (buttons), button, FALSE, FALSE, 0);
    }
    terminal_book = gtk_notebook_new ();
    gtk_widget_set_name (terminal_book, "terminal-notebook");
    g_object_add_weak_pointer (G_OBJECT (terminal_book), (gpointer *)&terminal_book);
    gtk_notebook_set_scrollable (GTK_NOTEBOOK (terminal_book), TRUE);
    GtkWidget *log_label = gtk_label_new (_("Application output"));
    gtk_widget_set_tooltip_text (log_label, _("Command and file-operation log. For interactive programs, use Open terminal here and type at the shell prompt."));
    gtk_notebook_append_page (GTK_NOTEBOOK (terminal_book), output, log_label);
    gtk_box_pack_start (GTK_BOX (box), buttons, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (box), terminal_book, TRUE, TRUE, 0);
    return box;
}
gboolean e2_terminal_confirm_shutdown (void)
{
    GList *member;
    for (member = sessions; member != NULL; member = member->next)
    {
        TerminalSession *session = member->data;
        if (session->pending || session->pid > 0)
            return confirm_close (_("Terminal shells are still running. Quitting hangs up their shells and terminal jobs. Quit?"));
    }
    return TRUE;
}
void e2_terminal_shutdown (void)
{
    while (sessions != NULL)
        gtk_widget_destroy (((TerminalSession *)sessions->data)->page);
}
void e2_terminal_actions_register (void)
{
    E2_Action actions[] = {
        {g_strdup ("terminal.open_here"), open_here, FALSE, E2_ACTION_TYPE_ITEM, 0, NULL, NULL},
        {g_strdup ("terminal.focus"), focus_terminal, FALSE, E2_ACTION_TYPE_ITEM, 0, NULL, NULL},
        {g_strdup ("terminal.close"), close_terminal, FALSE, E2_ACTION_TYPE_ITEM, 0, NULL, NULL},
        {g_strdup ("terminal.restart"), restart_terminal, FALSE, E2_ACTION_TYPE_ITEM, 0, NULL, NULL},
        {g_strdup ("terminal.insert_paths"), insert_paths, FALSE, E2_ACTION_TYPE_ITEM, 0, NULL, NULL}
    };
    guint i;
    for (i = 0; i < G_N_ELEMENTS (actions); i++) e2_action_register (&actions[i]);
}
void e2_terminal_options_register (void)
{
    gchar *group = g_strconcat (_C(6), ".", _("terminal"), NULL);
    e2_option_str_register ("terminal-shell", group, _("shell executable"),
        _("Executable path only; empty uses SHELL or /bin/sh. New sessions start in the active local pane."),
        NULL, "", E2_OPTION_FLAG_BASIC | E2_OPTION_FLAG_FREEGROUP);
    e2_option_int_register ("terminal-scrollback", group, _("scrollback lines"),
        NULL, NULL, 10000, 0, 1000000, E2_OPTION_FLAG_BASIC);
    e2_option_font_register ("terminal-font", group, _("terminal font"),
        _("Terminal preferences apply to new or restarted sessions"), NULL, "Monospace 10", E2_OPTION_FLAG_BASIC);
    e2_option_color_register ("terminal-foreground", group, _("terminal foreground"),
        NULL, NULL, "#dddddd", E2_OPTION_FLAG_BASIC);
    e2_option_color_register ("terminal-background", group, _("terminal background"),
        NULL, NULL, "#202020", E2_OPTION_FLAG_BASIC);
}
#endif
