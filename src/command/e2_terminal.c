/* Optional terminal sessions. SPDX-License-Identifier: GPL-3.0-or-later */
#include "e2_terminal.h"
#ifdef E2_VTE
#include "e2_terminal_backend.h"
#include "e2_terminal_context.h"
#include "e2_terminal_search.h"
#include "e2_terminal_shell.h"
#include "e2_tabs.h"
#include <glib/gstdio.h>
#include "e2_action.h"
#include "e2_option.h"
#include "e2_fileview.h"
#include "e2_window.h"
#include "e2_dialog.h"
#include "e2_icons.h"
#include "e2_pane.h"
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

typedef struct
{
    GtkWidget *book, *output, *frame, *search, *log_badge;
    GtkTextBuffer *log_buffer;
    GtkWidget *log_view;
    gboolean log_unread;
    guint pane;
} TerminalPane;
struct _E2_TerminalWorkspace
{
    GtkWidget *widget;
    TerminalPane panes[2];
    gboolean expanded;
    gdouble normal_ratio;
    gboolean moving_divider;
};

/* VTE sessions stay separate from the text-only application output runtimes. */
typedef struct
{
    gint refs;
    GtkWidget *page, *terminal, *label, *status, *statusbar, *restart_button;
    gchar *directory, *shell, *shell_rc;
    GtkWidget *badge;
    gboolean unread, content_changed;
    guint content_hash;
    GCancellable *cancel;
    GPid pid;
    gboolean pending, disposed, exited;
    E2_TerminalContext *context;
    const gchar *state;
    TerminalPane *owner;
} TerminalSession;
static E2_TerminalWorkspace *workspace;
static GList *sessions, *workspaces;
static gboolean dispatch_action (gpointer data);
static void button_action (GtkWidget *button, gpointer data);
static gboolean restart_terminal (gpointer from, E2_ActionRuntime *art);
static gboolean find_in_tool (gpointer from, E2_ActionRuntime *art);
static guint title_source;
static gboolean syncing_layout, selecting_workspace;
static pthread_t ui_thread;

void e2_terminal_sync_layout (void)
{
    if (workspace == NULL || app.window.rebuilding || workspace->expanded || syncing_layout
        || app.window.panes_horizontal || app.window.panes_paned == NULL
        || !gtk_widget_get_mapped (app.window.panes_paned)) return;
    gint x, y;
    if (gtk_widget_translate_coordinates (app.window.panes_paned, workspace->widget,
        gtk_paned_get_position (GTK_PANED (app.window.panes_paned)), 0, &x, &y))
    {
        syncing_layout = TRUE;
        gtk_paned_set_position (GTK_PANED (workspace->widget), MAX (0, x));
        syncing_layout = FALSE;
    }
}
static void tools_allocated (GtkWidget *widget, GtkAllocation *allocation, gpointer data)
{
    if (workspace != NULL && widget == workspace->widget) e2_terminal_sync_layout ();
}
static void tools_divider_changed (GObject *object, GParamSpec *property, gpointer data)
{
    if (workspace == NULL || GTK_WIDGET (object) != workspace->widget || syncing_layout
        || !workspace->moving_divider || app.window.rebuilding || workspace->expanded || app.window.panes_horizontal || app.window.panes_paned == NULL
        || !gtk_widget_get_mapped (workspace->widget)) return;
    gint x, y;
    if (gtk_widget_translate_coordinates (workspace->widget, app.window.panes_paned,
        gtk_paned_get_position (GTK_PANED (workspace->widget)), 0, &x, &y))
    {
        syncing_layout = TRUE;
        app.window.panes_equal = FALSE;
        gtk_paned_set_position (GTK_PANED (app.window.panes_paned), MAX (0, x));
        syncing_layout = FALSE;
    }
}
static void tools_equalize (GtkMenuItem *item, GtkWidget *widget)
{
    if (workspace == NULL || widget != workspace->widget || workspace->expanded) return;
    CLOSEBGL_IF_OPEN
    gint maxpos;
    g_object_get (widget, "max-position", &maxpos, NULL);
    if (maxpos > 0)
    {
        workspace->moving_divider = TRUE;
        gtk_paned_set_position (GTK_PANED (widget), maxpos / 2);
        tools_divider_changed (G_OBJECT (widget), NULL, NULL);
        workspace->moving_divider = FALSE;
    }
    OPENBGL_IF_CLOSED
}
static gboolean tools_divider_press (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
#ifdef USE_GTK2_22
    GdkWindow *handle = gtk_paned_get_handle_window (GTK_PANED (widget));
#else
    GdkWindow *handle = GTK_PANED (widget)->handle;
#endif
    if (workspace == NULL || widget != workspace->widget || workspace->expanded
        || event->type != GDK_BUTTON_PRESS
        || event->window != handle) return FALSE;
    if (event->button == 1)
    {
        workspace->moving_divider = TRUE;
        return FALSE;
    }
    if (event->button != 3) return FALSE;
    CLOSEBGL_IF_OPEN
    GtkWidget *menu = e2_menu_get ();
    gtk_menu_attach_to_widget (GTK_MENU (menu), widget, NULL);
    g_signal_connect_object (widget, "destroy", G_CALLBACK (gtk_widget_destroy), menu, G_CONNECT_SWAPPED);
    GtkWidget *item = e2_menu_add (menu, _("Equal panel sizes (1:1)"), NULL,
        _("Divide the tools area into two equal widths"), NULL, NULL);
    g_signal_connect_object (item, "activate", G_CALLBACK (tools_equalize), widget, 0);
    e2_menu_popup (menu, event->button, event->time);
    OPENBGL_IF_CLOSED
    return TRUE;
}
static gboolean tools_divider_release (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    if (workspace != NULL && widget == workspace->widget)
    {
        tools_divider_changed (G_OBJECT (widget), NULL, NULL);
        workspace->moving_divider = FALSE;
    }
    return FALSE;
}
static gboolean tools_handle_key (GtkWidget *widget, GtkScrollType scroll, gpointer data)
{
    if (workspace != NULL && widget == workspace->widget)
    {
        workspace->moving_divider = TRUE;
        tools_divider_changed (G_OBJECT (widget), NULL, NULL);
        workspace->moving_divider = FALSE;
    }
    return FALSE;
}
static TerminalPane *active_pane (void)
{
    return workspace == NULL ? NULL : &workspace->panes[curr_pane == &app.pane2];
}
void e2_terminal_select_pane (void)
{
    TerminalPane *pane = active_pane ();
    if (pane != NULL) e2_output_select_notebook (pane->output);
    if (workspace != NULL)
        for (guint i = 0; i < 2; i++)
            if (workspace->panes[i].frame != NULL) gtk_widget_queue_draw (workspace->panes[i].frame);
}
static void activate_pane (TerminalPane *pane)
{
    if ((curr_pane == &app.pane2) != (pane->pane == 1))
    {
        CLOSEBGL_IF_OPEN
        e2_pane_activate_other ();
        OPENBGL_IF_CLOSED
    }
    e2_output_select_notebook (pane->output);
}
void e2_terminal_select_output (GtkWidget *output)
{
    TerminalPane *pane = g_object_get_data (G_OBJECT (output), "terminal-pane");
    if (pane != NULL && pane != active_pane ()) activate_pane (pane);
}
static gboolean terminal_focused (GtkWidget *widget, GdkEventFocus *event, TerminalSession *session)
{
    activate_pane (session->owner);
    return FALSE;
}
static void output_focused (GtkWindow *window, GtkWidget *focus, gpointer data)
{
    if (app.window.rebuilding || selecting_workspace) return;
    if (workspace != NULL)
        for (guint i = 0; i < 2; i++) gtk_widget_queue_draw (workspace->panes[i].frame);
    for (GtkWidget *widget = focus; widget != NULL; widget = gtk_widget_get_parent (widget))
    {
        TerminalPane *pane = g_object_get_data (G_OBJECT (widget), "terminal-pane");
        if (pane != NULL)
        {
            activate_pane (pane);
            return;
        }
    }
}

static void session_unref (TerminalSession *session)
{
    if (--session->refs != 0) return;
    g_object_unref (session->cancel);
    g_free (session->directory);
    g_free (session->shell);
    if (session->shell_rc != NULL) g_unlink (session->shell_rc);
    g_free (session->shell_rc);
    e2_terminal_context_free (session->context);
    g_free (session);
}
static void session_refresh_title (TerminalSession *session)
{
    if (session->pid > 0)
    {
        GPid pid = e2_terminal_backend_foreground_pid (session->terminal);
        if (pid > 0)
            e2_terminal_context_update (session->context, pid,
                e2_terminal_backend_title (session->terminal),
                e2_terminal_backend_directory_uri (session->terminal));
    }
    gchar *text = e2_terminal_context_label (session->context);
    if (strcmp (text, gtk_label_get_text (GTK_LABEL (session->label))))
        gtk_label_set_text (GTK_LABEL (session->label), text);
    g_free (text);
    gchar *description = e2_terminal_context_description (session->context);
    gchar *tip = g_strconcat (session->state != NULL ? session->state : "", "\n", description, NULL);
    gtk_widget_set_tooltip_text (session->label, tip);
    g_free (description); g_free (tip);
    gchar *directory = e2_terminal_context_local_directory (session->context);
    gchar *display = g_filename_display_name (directory != NULL ? directory : session->directory);
    tip = g_strdup_printf (directory != NULL ? _("Restart in %s") :
        _("Current local folder unavailable. Restart in startup folder: %s"), display);
    gtk_widget_set_tooltip_text (session->restart_button, tip);
    g_free (display); g_free (directory); g_free (tip);
}
static gboolean view_seen (TerminalPane *pane, GtkWidget *page)
{
    if (app.window.rebuilding || !app.output.visible || !gtk_widget_get_mapped (page)) return FALSE;
    GtkAllocation allocation;
    gtk_widget_get_allocation (page, &allocation);
    return allocation.height > 10 && gtk_notebook_get_nth_page (GTK_NOTEBOOK (pane->book),
        gtk_notebook_get_current_page (GTK_NOTEBOOK (pane->book))) == page;
}
static void content_changed (GtkWidget *terminal, TerminalSession *session)
{
    session->content_changed = TRUE;
}
static void log_inserted (GtkTextBuffer *buffer, GtkTextIter *location, gchar *text, gint length, TerminalPane *pane)
{
    if (length > 0 && !view_seen (pane, gtk_notebook_get_nth_page (GTK_NOTEBOOK (pane->book), 0)))
        pane->log_unread = TRUE;
}
static void log_buffer_changed (GObject *text, GParamSpec *property, TerminalPane *pane)
{
#ifdef USE_GTK3_0
    if (gtk_widget_in_destruction (GTK_WIDGET (text))) return;
#else
    if (GTK_OBJECT_FLAGS (text) & GTK_IN_DESTRUCTION) return;
#endif
    if (pane->log_buffer != NULL)
    {
        g_signal_handlers_disconnect_by_data (pane->log_buffer, pane);
        g_object_unref (pane->log_buffer);
    }
    pane->log_buffer = g_object_ref (gtk_text_view_get_buffer (GTK_TEXT_VIEW (text)));
    pane->log_unread = FALSE;
    g_signal_connect_after (pane->log_buffer, "insert-text", G_CALLBACK (log_inserted), pane);
}
static void disconnect_log (TerminalPane *pane)
{
    g_signal_handlers_disconnect_by_data (pane->log_view, pane);
    g_signal_handlers_disconnect_by_data (pane->log_buffer, pane);
    g_object_unref (pane->log_buffer);
    pane->log_buffer = NULL;
}
static void update_activity (void)
{
    for (GList *link = sessions; link != NULL; link = link->next)
    {
        TerminalSession *session = link->data;
        if (session->content_changed)
        {
            /* contents-changed can also follow a resize. Compare retained text
             * so repainting a hidden terminal does not invent unread output. */
            gchar *text = e2_terminal_backend_text (session->terminal);
            if (text != NULL) g_strchomp (text); //ignore blank cells added by a resize
            guint hash = text != NULL ? g_str_hash (text) : 0;
            if (hash != session->content_hash && text != NULL && *text) session->unread = TRUE;
            session->content_hash = hash;
            session->content_changed = FALSE;
            g_free (text);
        }
        if (view_seen (session->owner, session->page)) session->unread = FALSE;
        gtk_label_set_text (GTK_LABEL (session->badge), session->unread ? "●" : "");
    }
    for (GList *link = workspaces; link != NULL; link = link->next)
    {
        E2_TerminalWorkspace *space = link->data;
        gboolean unread = FALSE;
        for (guint i = 0; i < 2; i++)
        {
            TerminalPane *pane = &space->panes[i];
            if (view_seen (pane, gtk_notebook_get_nth_page (GTK_NOTEBOOK (pane->book), 0))) pane->log_unread = FALSE;
            gtk_label_set_text (GTK_LABEL (pane->log_badge), pane->log_unread ? "●" : "");
            unread |= pane->log_unread;
            for (GList *member = sessions; member != NULL; member = member->next)
            {
                TerminalSession *session = member->data;
                if (session->owner == pane) unread |= session->unread;
            }
        }
        e2_tabs_output_activity (space, unread);
    }
}
static GtkWidget *activity_badge (void)
{
    GtkWidget *badge = gtk_label_new ("");
    gtk_widget_set_name (badge, "tools-unread");
    gtk_widget_set_tooltip_text (badge, _("Unread output"));
    atk_object_set_name (gtk_widget_get_accessible (badge), _("Unread output"));
    return badge;
}
static gboolean refresh_titles (gpointer data)
{
    CLOSEBGL_IF_OPEN
    for (GList *link = sessions; link != NULL; link = link->next)
        session_refresh_title (link->data);
    update_activity ();
    OPENBGL_IF_CLOSED
    return TRUE;
}
static void session_label (TerminalSession *session, const gchar *state)
{
    session->state = state;
    session_refresh_title (session);
}
static void session_exited (gint status, gboolean known, gpointer data)
{
    TerminalSession *session = data;
    if (session->disposed) return;
    session_refresh_title (session);
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
    gtk_widget_show (session->statusbar);
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
            gtk_widget_show (session->statusbar);
        }
        else if (!session->exited)
        {
            session->pid = pid;
            session_label (session, _("running"));
            gtk_label_set_text (GTK_LABEL (session->status), "");
            gtk_widget_hide (session->statusbar);
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
    g_signal_handlers_disconnect_by_data (widget, session);
    g_cancellable_cancel (session->cancel);
    sessions = g_list_remove (sessions, session);
    /* Destroying the PTY sends a hangup to its foreground process group.
     * VTE retains the child watch; we never waitpid() a terminal child. */
    if (session->pid > 0) kill (session->pid, SIGHUP);
    session->pid = 0;
    g_signal_handlers_disconnect_by_data (session->terminal, session);
    /* The widget may stay referenced by an async spawn. Suppress exit callbacks
     * before releasing the session's widget ownership. */
    e2_terminal_backend_disconnect (session->terminal);
    session_unref (session);
}
static TerminalSession *current_session (void)
{
    TerminalPane *pane = active_pane ();
    if (pane == NULL) return NULL;
    GtkWidget *page = gtk_notebook_get_nth_page (GTK_NOTEBOOK (pane->book),
        gtk_notebook_get_current_page (GTK_NOTEBOOK (pane->book)));
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
            case GDK_F6:
                e2_terminal_restore_tools ();
                gtk_widget_grab_focus (curr_view->treeview);
                return TRUE;
            case GDK_f: find_in_tool (NULL, NULL); return TRUE;
            case GDK_c: e2_terminal_backend_copy (widget); return TRUE;
            case GDK_v: e2_terminal_backend_paste (widget); return TRUE;
        }
    }
    return FALSE; /* VTE gets Ctrl+C, Ctrl+Z, Tab, function keys, etc. */
}
static void terminal_copy (GtkMenuItem *item, GtkWidget *terminal)
{
    e2_terminal_backend_copy (terminal);
}
static void terminal_paste (GtkMenuItem *item, GtkWidget *terminal)
{
    e2_terminal_backend_paste (terminal);
}
static gboolean show_terminal_menu (GtkWidget *terminal, gpointer data, GtkWidget *anchor)
{
    if (data != NULL) activate_pane (((TerminalSession *)data)->owner);
    GtkWidget *menu = e2_menu_get ();
    g_object_set_data_full (G_OBJECT (menu), "terminal", g_object_ref (terminal), g_object_unref);
    GtkWidget *copy = e2_menu_add (menu, _("_Copy"), STOCK_NAME_COPY,
        _("Copy selected terminal text"), terminal_copy, terminal);
    gtk_widget_set_sensitive (copy, e2_terminal_backend_has_selection (terminal));
    e2_menu_add (menu, _("_Paste"), STOCK_NAME_PASTE,
        _("Paste clipboard text into the terminal"), terminal_paste, terminal);
    e2_menu_add_action (menu, _("_Find"), STOCK_NAME_FIND,
        _("Find text in this terminal (Ctrl+Shift+F)"), "terminal.find", NULL);
    e2_menu_add_separator (menu);
    e2_menu_add_action (menu, _("Insert selected paths"), STOCK_NAME_ADD,
        _("Insert quoted file paths without executing them"), "terminal.insert_paths", NULL);
    TerminalSession *session = current_session ();
    gchar *directory = session == NULL ? NULL : e2_terminal_context_local_directory (session->context);
    GtkWidget *folder = e2_menu_add_action (menu, _("Show terminal folder in pane"), STOCK_NAME_DIRECTORY,
        _("Navigate this file pane to the terminal's current local folder"), "terminal.show_folder", NULL);
    gtk_widget_set_sensitive (folder, directory != NULL);
    g_free (directory);
    gchar *restart_tip = session == NULL ? NULL : gtk_widget_get_tooltip_text (session->restart_button);
    e2_menu_add_action (menu, _("_Restart"), STOCK_NAME_REFRESH, restart_tip, "terminal.restart", NULL);
    g_free (restart_tip);
    e2_menu_add_action (menu, _("_Close terminal"), STOCK_NAME_CLOSE,
        _("Close this terminal"), "terminal.close", NULL);
    e2_menu_add_separator (menu);
    e2_menu_add_action (menu, _("_Hide tools"), "output_hide"E2ICONTB,
        _("Return to the file list"), "terminal.hide_tools", NULL);
    e2_menu_add_action (menu, workspace->expanded ? _("_Restore tools") : _("_Expand view"), STOCK_NAME_ZOOM_FIT,
        _("Expand this view, or restore the file lists and both tools areas"), "terminal.expand_tools", NULL);
    g_signal_connect (menu, "selection-done", G_CALLBACK (e2_menu_selection_done_cb), NULL);
    if (anchor != NULL) e2_menu_popup_below (menu, anchor);
    else gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 0, gtk_get_current_event_time ());
    return TRUE;
}
static gboolean terminal_popup_menu (GtkWidget *terminal, gpointer data)
{
    return show_terminal_menu (terminal, data, NULL);
}
static gboolean terminal_button (GtkWidget *terminal, GdkEventButton *event, gpointer data)
{
    if (event->type != GDK_BUTTON_PRESS || event->button != 3) return FALSE;
    return terminal_popup_menu (terminal, data);
}
gboolean e2_terminal_show_menu (void)
{
    TerminalSession *session = current_session ();
    return session != NULL && terminal_popup_menu (session->terminal, session);
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
static gboolean session_has_jobs (TerminalSession *session)
{
    return !session->disposed && e2_terminal_context_has_jobs (session->pid,
        e2_terminal_backend_foreground_pid (session->terminal), session->shell);
}
static gboolean session_can_close (TerminalSession *session)
{
    return !session_has_jobs (session) || confirm_close (
        _("A program is still running in this terminal. Closing it hangs up its shell and terminal jobs. Close it?"));
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
static void restart_session_clicked (GtkButton *button, TerminalSession *session)
{
    activate_pane (session->owner);
    gtk_notebook_set_current_page (GTK_NOTEBOOK (session->owner->book),
        gtk_notebook_page_num (GTK_NOTEBOOK (session->owner->book), session->page));
    restart_terminal (NULL, NULL);
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
static void reveal_terminal (gboolean new_session)
{
    e2_window_output_show (NULL, NULL);
    /* The default log pane is very short. Give a newly focused terminal room
     * for interactive programs without changing the default startup layout. */
    if (new_session && app.window.output_paned != NULL)
    {
        GtkAllocation allocation;
        gtk_widget_get_allocation (app.window.output_paned, &allocation);
        GtkRequisition files;
        gtk_widget_size_request (gtk_paned_get_child1 (GTK_PANED (app.window.output_paned)), &files);
        gint wanted = MIN (300, allocation.height / 2);
        wanted = MIN (wanted, MAX (1, allocation.height - files.height - 8));
        GtkPaned *paned = GTK_PANED (app.window.output_paned);
        if (allocation.height - gtk_paned_get_position (paned) < wanted)
            gtk_paned_set_position (paned, allocation.height - wanted);
    }
}
static void open_session (TerminalPane *pane, const gchar *directory)
{
    TerminalSession *session = g_new0 (TerminalSession, 1);
    session->refs = 1;
    session->owner = pane;
    session->directory = g_strdup (directory);
    session->context = e2_terminal_context_new (directory);
    const gchar *shell = e2_option_str_get ("terminal-shell");
    if (shell == NULL || *shell == '\0') shell = g_getenv ("SHELL");
    if (shell == NULL || *shell == '\0') shell = "/bin/sh";
    session->shell = g_strdup (shell);
    session->cancel = g_cancellable_new ();
    session->page = gtk_vbox_new (FALSE, 0);
    session->label = gtk_label_new ("");
    gtk_label_set_ellipsize (GTK_LABEL (session->label), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_width_chars (GTK_LABEL (session->label), 10);
    gtk_label_set_max_width_chars (GTK_LABEL (session->label), 32);
    session->status = gtk_label_new (_("Starting shell…"));
    session->statusbar = gtk_hbox_new (FALSE, 4);
    gtk_box_pack_start (GTK_BOX (session->statusbar), session->status, TRUE, TRUE, 0);
    session->restart_button = e2_button_add (session->statusbar, FALSE, 0, _("Restart"), STOCK_NAME_REFRESH,
        _("Restart this terminal"), restart_session_clicked, session);
    e2_button_add (session->statusbar, FALSE, 0, _("Close"), STOCK_NAME_CLOSE,
        _("Close this terminal"), close_session_clicked, session);
    gtk_widget_show_all (session->statusbar);
    gtk_widget_set_no_show_all (session->statusbar, TRUE);
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
    gtk_box_pack_start (GTK_BOX (session->page), session->statusbar, FALSE, FALSE, 0);
    g_object_set_data (G_OBJECT (session->page), "e2-terminal-session", session);
    g_signal_connect (session->page, "destroy", G_CALLBACK (session_destroyed), session);
    g_signal_connect (session->terminal, "key-press-event", G_CALLBACK (terminal_key), session);
    g_signal_connect (session->terminal, "button-press-event", G_CALLBACK (terminal_button), session);
    g_signal_connect (session->terminal, "popup-menu", G_CALLBACK (terminal_popup_menu), session);
    g_signal_connect (session->terminal, "focus-in-event", G_CALLBACK (terminal_focused), session);
    g_signal_connect (session->terminal, "contents-changed", G_CALLBACK (content_changed), session);
    sessions = g_list_append (sessions, session);
    if (title_source == 0) title_source = g_timeout_add (500, refresh_titles, NULL);
    session_label (session, _("starting"));
    GtkWidget *title = gtk_hbox_new (FALSE, 4);
    GtkWidget *close = e2_button_get_full (NULL, STOCK_NAME_CLOSE, GTK_ICON_SIZE_MENU,
        _("Close this terminal"), close_session_clicked, session, E2_BUTTON_SHOW_MISSING_ICON);
    gtk_widget_set_name (close, "terminal-close");
    atk_object_set_name (gtk_widget_get_accessible (close), _("Close terminal"));
    gtk_button_set_relief (GTK_BUTTON (close), GTK_RELIEF_NONE);
    gtk_box_pack_start (GTK_BOX (title), session->label, TRUE, TRUE, 0);
    session->badge = activity_badge ();
    gtk_box_pack_start (GTK_BOX (title), session->badge, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (title), close, FALSE, FALSE, 0);
    gtk_widget_show_all (title);
    gint page = gtk_notebook_append_page (GTK_NOTEBOOK (pane->book), session->page, title);
    gtk_widget_show_all (session->page);
    gtk_notebook_set_current_page (GTK_NOTEBOOK (pane->book), page);
    reveal_terminal (TRUE);
    gtk_widget_grab_focus (session->terminal);
    gchar *argv[] = { session->shell, "-i", NULL, NULL, NULL };
    gchar *base = g_path_get_basename (session->shell);
    GError *error = NULL;
    if (e2_option_bool_get ("terminal-shell-integration") && !strcmp (base, "bash"))
    {
        gchar *rc = g_build_filename (g_get_home_dir (), ".bashrc", NULL);
        session->shell_rc = e2_terminal_shell_rc (rc, &error);
        g_free (rc);
        if (session->shell_rc != NULL)
        {
            argv[1] = "--rcfile"; argv[2] = session->shell_rc; argv[3] = "-i";
        }
    }
    g_free (base);
    session->pending = TRUE;
    session->refs++; /* async callback owns this even after the page is closed */
    if (error != NULL)
    {
        session_spawned (-1, error, session);
        g_error_free (error);
        return;
    }
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
    open_session (active_pane (), local);
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
    TerminalPane *pane = active_pane ();
    GList *link;
    for (link = g_list_last (sessions); session == NULL && link != NULL; link = link->prev)
        if (((TerminalSession *)link->data)->owner == pane) session = link->data;
    if (session == NULL) return open_here (from, art);
    gtk_notebook_set_current_page (GTK_NOTEBOOK (pane->book),
        gtk_notebook_page_num (GTK_NOTEBOOK (pane->book), session->page));
    reveal_terminal (FALSE);
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
        gchar *directory = e2_terminal_context_local_directory (session->context);
        if (directory == NULL) directory = g_strdup (session->directory);
        gtk_widget_destroy (session->page);
        activate_pane (session->owner);
        open_session (session->owner, directory);
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
static void focus_selected_tool (void)
{
    TerminalSession *session = current_session ();
    gtk_widget_grab_focus (session != NULL ? session->terminal : GTK_WIDGET (app.tab.text));
}
void e2_terminal_restore_tools (void)
{
    if (workspace == NULL || !workspace->expanded) return;
    workspace->expanded = FALSE;
    GtkWidget *files = gtk_paned_get_child1 (GTK_PANED (app.window.output_paned));
    gtk_widget_set_no_show_all (files, FALSE);
    gtk_widget_show (files);
    for (guint i = 0; i < 2; i++)
    {
        gtk_widget_set_no_show_all (workspace->panes[i].frame, FALSE);
        gtk_widget_show (workspace->panes[i].frame);
    }
    GtkAllocation allocation;
    gtk_widget_get_allocation (app.window.output_paned, &allocation);
    app.window.output_paned_ratio_last = workspace->normal_ratio;
    gtk_paned_set_position (GTK_PANED (app.window.output_paned), allocation.height * workspace->normal_ratio);
    e2_terminal_sync_layout ();
}
static gboolean show_folder (gpointer from, E2_ActionRuntime *art)
{
    if (!pthread_equal (pthread_self (), ui_thread)) { g_idle_add (dispatch_action, GUINT_TO_POINTER (7)); return TRUE; }
    TerminalSession *session = current_session ();
    if (session == NULL) return FALSE;
    session_refresh_title (session);
    gchar *directory = e2_terminal_context_local_directory (session->context);
    if (directory == NULL) return FALSE;
    e2_terminal_restore_tools ();
    gchar *utf = F_FILENAME_FROM_LOCALE (directory);
    CLOSEBGL_IF_OPEN
    e2_pane_change_dir (curr_pane, utf);
    OPENBGL_IF_CLOSED
    F_FREE (utf, directory);
    g_free (directory);
    gtk_widget_grab_focus (curr_view->treeview);
    return TRUE;
}
static gboolean hide_tools (gpointer from, E2_ActionRuntime *art)
{
    if (!pthread_equal (pthread_self (), ui_thread)) { g_idle_add (dispatch_action, GUINT_TO_POINTER (8)); return TRUE; }
    e2_terminal_restore_tools ();
    CLOSEBGL_IF_OPEN
    e2_window_output_hide (NULL, NULL, NULL);
    OPENBGL_IF_CLOSED
    return TRUE;
}
static gboolean expand_tools (gpointer from, E2_ActionRuntime *art)
{
    if (!pthread_equal (pthread_self (), ui_thread)) { g_idle_add (dispatch_action, GUINT_TO_POINTER (9)); return TRUE; }
    if (workspace->expanded) e2_terminal_restore_tools ();
    else
    {
        GtkAllocation allocation;
        gtk_widget_get_allocation (app.window.output_paned, &allocation);
        gdouble ratio = (gdouble) gtk_paned_get_position (GTK_PANED (app.window.output_paned)) / MAX (1, allocation.height);
        workspace->normal_ratio = (ratio > 0.02 && ratio < 0.98) ? ratio : app.window.output_paned_ratio_last;
        workspace->expanded = TRUE;
        GtkWidget *files = gtk_paned_get_child1 (GTK_PANED (app.window.output_paned));
        gtk_widget_set_no_show_all (files, TRUE);
        gtk_widget_hide (files);
        TerminalPane *other = &workspace->panes[curr_pane != &app.pane2];
        gtk_widget_set_no_show_all (other->frame, TRUE);
        gtk_widget_hide (other->frame);
        gtk_paned_set_position (GTK_PANED (app.window.output_paned), 0);
    }
    focus_selected_tool ();
    return TRUE;
}
static gboolean cycle_tools (gpointer from, E2_ActionRuntime *art)
{
    gint delta = GPOINTER_TO_INT (art->action->data);
    if (!pthread_equal (pthread_self (), ui_thread))
    { g_idle_add (dispatch_action, GUINT_TO_POINTER (delta < 0 ? 11 : 10)); return TRUE; }
    GtkNotebook *book = GTK_NOTEBOOK (active_pane ()->book);
    gint count = gtk_notebook_get_n_pages (book);
    gtk_notebook_set_current_page (book, (gtk_notebook_get_current_page (book) + count + delta) % count);
    reveal_terminal (FALSE);
    focus_selected_tool ();
    return TRUE;
}
static gboolean find_in_tool (gpointer from, E2_ActionRuntime *art)
{
    if (!pthread_equal (pthread_self (), ui_thread)) { g_idle_add (dispatch_action, GUINT_TO_POINTER (12)); return TRUE; }
    TerminalPane *pane = active_pane ();
    if (pane == NULL) return FALSE;
    reveal_terminal (FALSE);
    TerminalSession *session = current_session ();
    e2_terminal_search_show (pane->search, session != NULL ? session->terminal : GTK_WIDGET (app.tab.text));
    return TRUE;
}
static void button_action (GtkWidget *button, gpointer data)
{
    if (button != NULL)
        activate_pane (g_object_get_data (G_OBJECT (button), "terminal-pane"));
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
        case 7: show_folder (NULL, NULL); break;
        case 8: hide_tools (NULL, NULL); break;
        case 9: expand_tools (NULL, NULL); break;
        case 12: find_in_tool (NULL, NULL); break;
        case 10:
        case 11:
        {
            GtkNotebook *book = GTK_NOTEBOOK (active_pane ()->book);
            gint count = gtk_notebook_get_n_pages (book), delta = action == 10 ? 1 : -1;
            gtk_notebook_set_current_page (book, (gtk_notebook_get_current_page (book) + count + delta) % count);
            reveal_terminal (FALSE);
            focus_selected_tool ();
            break;
        }
    }
}
static gboolean dispatch_action (gpointer data)
{
    if (workspace != NULL) button_action (NULL, data);
    return FALSE;
}
static void tools_menu (GtkWidget *button, TerminalPane *pane)
{
    activate_pane (pane);
    CLOSEBGL_IF_OPEN
    TerminalSession *session = current_session ();
    if (session != NULL) show_terminal_menu (session->terminal, session, button);
    else e2_output_show_log_menu (pane->book, button);
    OPENBGL_IF_CLOSED
}
static void tools_switched (GtkNotebook *book,
#ifdef USE_GTK3_0
    GtkWidget *page,
#else
    GtkNotebookPage *page,
#endif
    guint number, TerminalPane *pane)
{
    if (app.window.rebuilding || selecting_workspace || !gtk_widget_get_mapped (GTK_WIDGET (book))) return;
    activate_pane (pane);
    GtkWidget *child = gtk_notebook_get_nth_page (book, number);
    TerminalSession *session = child == NULL ? NULL : g_object_get_data (G_OBJECT (child), "e2-terminal-session");
    if (session != NULL) gtk_widget_grab_focus (session->terminal);
    else if (child != NULL) gtk_widget_grab_focus (GTK_WIDGET (app.tab.text));
    if (pane->search != NULL)
        e2_terminal_search_target (pane->search, session != NULL ? session->terminal : GTK_WIDGET (app.tab.text));
    if (session != NULL) session->unread = FALSE;
    else pane->log_unread = FALSE;
}
/* Selection and keyboard focus are deliberately separate. Use the theme's
 * focus rendering around whichever tools pane contains the keyboard focus. */
#ifdef USE_GTK3_0
static gboolean tools_focus_border (GtkWidget *frame, cairo_t *cr, gpointer data)
#else
static gboolean tools_focus_border (GtkWidget *frame, GdkEventExpose *event, gpointer data)
#endif
{
    GtkWidget *focus = gtk_window_get_focus (GTK_WINDOW (app.main_window));
    if (focus != NULL && gtk_widget_is_ancestor (focus, frame))
    {
        GtkAllocation allocation;
        gtk_widget_get_allocation (frame, &allocation);
#ifdef USE_GTK3_0
        gtk_render_focus (gtk_widget_get_style_context (frame), cr,
            1, 1, allocation.width - 2, allocation.height - 2);
#else
        gtk_paint_focus (gtk_widget_get_style (frame), gtk_widget_get_window (frame),
            GTK_STATE_NORMAL, &event->area, frame, "treeview",
            allocation.x + 1, allocation.y + 1, allocation.width - 2, allocation.height - 2);
#endif
    }
    return FALSE;
}
static GtkWidget *pane_create (TerminalPane *pane, GtkWidget *output, guint number)
{
    pane->pane = number;
    pane->output = output;
    g_object_set_data (G_OBJECT (output), "terminal-pane", pane);
    GtkWidget *box = gtk_vbox_new (FALSE, 0);
    g_object_set_data (G_OBJECT (box), "terminal-pane", pane);
    /* The existing log notebook becomes the tools notebook: no nested tabs. */
    e2_output_merge_notebooks (output, output);
    pane->book = output;
    gtk_widget_set_name (output, "terminal-notebook");
    gtk_notebook_set_tab_pos (GTK_NOTEBOOK (output), GTK_POS_TOP);
    gtk_notebook_set_show_tabs (GTK_NOTEBOOK (output), TRUE);
    gtk_notebook_set_show_border (GTK_NOTEBOOK (output), TRUE);
    gtk_notebook_popup_disable (GTK_NOTEBOOK (output));
    GtkWidget *log = gtk_notebook_get_nth_page (GTK_NOTEBOOK (output), 0);
    GtkWidget *log_label = gtk_label_new (_("Command log"));
    gtk_label_set_ellipsize (GTK_LABEL (log_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_width_chars (GTK_LABEL (log_label), 11);
    GtkWidget *title = gtk_hbox_new (FALSE, 4);
    pane->log_badge = activity_badge ();
    gtk_box_pack_start (GTK_BOX (title), log_label, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (title), pane->log_badge, FALSE, FALSE, 0);
    gtk_widget_show_all (title);
    gtk_notebook_set_tab_label (GTK_NOTEBOOK (output), log, title);
    E2_OutputTabRuntime *rt = g_object_get_data (G_OBJECT (log), "e2-output-tab");
    pane->log_view = GTK_WIDGET (rt->text);
    log_buffer_changed (G_OBJECT (rt->text), NULL, pane);
    g_signal_connect (rt->text, "notify::buffer", G_CALLBACK (log_buffer_changed), pane);
    gtk_notebook_set_tab_reorderable (GTK_NOTEBOOK (output), log, FALSE);
    gtk_widget_set_tooltip_text (log_label, _("Command and file-operation output. Type interactive commands directly in a terminal."));
    GtkWidget *buttons = gtk_hbox_new (FALSE, 0);
    gtk_widget_set_name (buttons, "terminal-actions");
    GtkWidget *add = e2_button_get_full (NULL, STOCK_NAME_ADD, GTK_ICON_SIZE_MENU,
        NULL, button_action, GUINT_TO_POINTER (0), E2_BUTTON_SHOW_MISSING_ICON);
    gtk_widget_set_name (add, "terminal-new");
    gtk_widget_set_tooltip_text (add, _("New terminal in this pane's folder"));
    atk_object_set_name (gtk_widget_get_accessible (add), _("New terminal"));
    g_object_set_data (G_OBJECT (add), "terminal-pane", pane);
    gtk_box_pack_start (GTK_BOX (buttons), add, FALSE, FALSE, 0);
    GtkWidget *menu = e2_button_get_full (NULL, STOCK_NAME_PROPERTIES, GTK_ICON_SIZE_MENU,
        NULL, tools_menu, pane, E2_BUTTON_SHOW_MISSING_ICON);
    gtk_widget_set_name (menu, "terminal-menu");
    gtk_widget_set_tooltip_text (menu, _("Actions for the selected log or terminal"));
    atk_object_set_name (gtk_widget_get_accessible (menu), _("View actions"));
    gtk_box_pack_start (GTK_BOX (buttons), menu, FALSE, FALSE, 0);
    gtk_widget_show_all (buttons);
    gtk_notebook_set_action_widget (GTK_NOTEBOOK (output), buttons, GTK_PACK_END);
    g_signal_connect_after (output, "switch-page", G_CALLBACK (tools_switched), pane);
    gtk_box_pack_start (GTK_BOX (box), output, TRUE, TRUE, 0);
    pane->search = e2_terminal_search_new ();
    gtk_box_pack_start (GTK_BOX (box), pane->search, FALSE, FALSE, 0);
    pane->frame = gtk_alignment_new (0.0, 0.0, 1.0, 1.0);
    gtk_alignment_set_padding (GTK_ALIGNMENT (pane->frame), 2, 2, 2, 2);
    gtk_container_add (GTK_CONTAINER (pane->frame), box);
#ifdef USE_GTK3_0
    g_signal_connect_after (pane->frame, "draw", G_CALLBACK (tools_focus_border), NULL);
#else
    g_signal_connect_after (pane->frame, "expose-event", G_CALLBACK (tools_focus_border), NULL);
#endif
    return pane->frame;
}
static E2_TerminalWorkspace *workspace_create (GtkWidget *output)
{
    E2_TerminalWorkspace *space = g_new0 (E2_TerminalWorkspace, 1);
    space->widget = gtk_hpaned_new ();
    g_object_ref_sink (space->widget); //survive removal from the main output area
    gtk_widget_set_name (space->widget, "terminal-split");
    GtkWidget *left = pane_create (&space->panes[0], output, 0);
    GtkWidget *right = pane_create (&space->panes[1], e2_output_create_notebook (1), 1);
    gtk_paned_pack1 (GTK_PANED (space->widget), left, TRUE, TRUE);
    gtk_paned_pack2 (GTK_PANED (space->widget), right, TRUE, TRUE);
    g_signal_connect (space->widget, "size-allocate", G_CALLBACK (tools_allocated), NULL);
    g_signal_connect (space->widget, "notify::position", G_CALLBACK (tools_divider_changed), NULL);
    g_signal_connect (space->widget, "button-press-event", G_CALLBACK (tools_divider_press), NULL);
    g_signal_connect_after (space->widget, "button-release-event", G_CALLBACK (tools_divider_release), NULL);
    g_signal_connect_after (space->widget, "move-handle", G_CALLBACK (tools_handle_key), NULL);
    workspaces = g_list_append (workspaces, space);
    if (title_source == 0) title_source = g_timeout_add (500, refresh_titles, NULL);
    return space;
}
GtkWidget *e2_terminal_wrap_output (GtkWidget *output)
{
    ui_thread = pthread_self ();
    workspace = workspace_create (output);
    e2_terminal_select_pane ();
    g_signal_connect (app.main_window, "set-focus", G_CALLBACK (output_focused), NULL);
    return workspace->widget;
}
E2_TerminalWorkspace *e2_terminal_workspace_current (void)
{
    return workspace;
}
E2_TerminalWorkspace *e2_terminal_workspace_new (void)
{
    return workspace_create (e2_output_create_notebook (1));
}
void e2_terminal_workspace_select (E2_TerminalWorkspace *space)
{
    if (space == NULL || space == workspace) return;
    e2_terminal_restore_tools ();
    gboolean visible = app.output.visible;
    selecting_workspace = TRUE;
    GtkWidget *parent = gtk_widget_get_parent (workspace->widget);
    if (parent != NULL) gtk_container_remove (GTK_CONTAINER (parent), workspace->widget);
    workspace = space;
    gtk_paned_pack2 (GTK_PANED (app.window.output_paned), space->widget, TRUE, TRUE);
    gtk_widget_set_no_show_all (space->widget, FALSE);
    gtk_widget_show_all (space->widget);
    if (!visible)
    {
        gtk_widget_set_no_show_all (space->widget, TRUE);
        gtk_widget_hide (space->widget);
    }
    selecting_workspace = FALSE;
    e2_terminal_select_pane ();
}
gboolean e2_terminal_workspace_can_close (E2_TerminalWorkspace *space)
{
    GList *link;
    for (link = sessions; link != NULL; link = link->next)
    {
        TerminalSession *session = link->data;
        if ((session->owner == &space->panes[0] || session->owner == &space->panes[1])
            && session_has_jobs (session))
            return confirm_close (_("Programs are still running in this tab's terminals. Closing it hangs up their shells and terminal jobs. Close it?"));
    }
    return TRUE;
}
void e2_terminal_workspace_close (E2_TerminalWorkspace *space)
{
    if (space == NULL || space == workspace) return;
    for (guint i = 0; i < 2; i++)
    {
        disconnect_log (&space->panes[i]);
        e2_output_destroy_notebook (space->panes[i].output, workspace->panes[i].output);
    }
    gtk_widget_destroy (space->widget);
    g_object_unref (space->widget);
    workspaces = g_list_remove (workspaces, space);
    g_free (space);
}
void e2_terminal_workspace_merge (E2_TerminalWorkspace *space)
{
    if (space == NULL || space == workspace) return;
    for (guint i = 0; i < 2; i++)
    {
        TerminalPane *source = &space->panes[i], *destination = &workspace->panes[i];
        disconnect_log (source);
        destination->log_unread |= source->log_unread;
        e2_output_merge_notebooks (source->output, destination->output);
        GList *link;
        for (link = sessions; link != NULL; link = link->next)
        {
            TerminalSession *session = link->data;
            if (session->owner != source) continue;
            GtkWidget *label = gtk_notebook_get_tab_label (GTK_NOTEBOOK (source->book), session->page);
            g_object_ref (label);
            g_object_ref (session->page);
            gtk_container_remove (GTK_CONTAINER (source->book), session->page);
            session->owner = destination;
            gtk_notebook_append_page (GTK_NOTEBOOK (destination->book), session->page, label);
            g_object_unref (session->page);
            g_object_unref (label);
        }
    }
    gtk_widget_destroy (space->widget);
    g_object_unref (space->widget);
    workspaces = g_list_remove (workspaces, space);
    g_free (space);
    e2_terminal_select_pane ();
}
gboolean e2_terminal_confirm_shutdown (void)
{
    GList *member;
    for (member = sessions; member != NULL; member = member->next)
    {
        TerminalSession *session = member->data;
        if (session_has_jobs (session))
            return confirm_close (_("Programs are still running in terminals. Quitting hangs up their shells and terminal jobs. Quit?"));
    }
    return TRUE;
}
void e2_terminal_shutdown (void)
{
    if (title_source != 0) { g_source_remove (title_source); title_source = 0; }
    while (sessions != NULL)
        gtk_widget_destroy (((TerminalSession *)sessions->data)->page);
}
void e2_terminal_actions_register (void)
{
    E2_Action actions[] = {
        {g_strdup ("terminal.find"), find_in_tool, FALSE, E2_ACTION_TYPE_ITEM, 0, NULL, NULL},
        {g_strdup ("terminal.open_here"), open_here, FALSE, E2_ACTION_TYPE_ITEM, 0, NULL, NULL},
        {g_strdup ("terminal.focus"), focus_terminal, FALSE, E2_ACTION_TYPE_ITEM, 0, NULL, NULL},
        {g_strdup ("terminal.close"), close_terminal, FALSE, E2_ACTION_TYPE_ITEM, 0, NULL, NULL},
        {g_strdup ("terminal.restart"), restart_terminal, FALSE, E2_ACTION_TYPE_ITEM, 0, NULL, NULL},
        {g_strdup ("terminal.insert_paths"), insert_paths, FALSE, E2_ACTION_TYPE_ITEM, 0, NULL, NULL},
        {g_strdup ("terminal.show_folder"), show_folder, FALSE, E2_ACTION_TYPE_ITEM, 0, NULL, NULL},
        {g_strdup ("terminal.hide_tools"), hide_tools, FALSE, E2_ACTION_TYPE_ITEM, 0, NULL, NULL},
        {g_strdup ("terminal.expand_tools"), expand_tools, FALSE, E2_ACTION_TYPE_ITEM, 0, NULL, NULL},
        {g_strdup ("terminal.next_tab"), cycle_tools, FALSE, E2_ACTION_TYPE_ITEM, 0, GINT_TO_POINTER (1), NULL},
        {g_strdup ("terminal.previous_tab"), cycle_tools, FALSE, E2_ACTION_TYPE_ITEM, 0, GINT_TO_POINTER (-1), NULL}
    };
    guint i;
    for (i = 0; i < G_N_ELEMENTS (actions); i++) e2_action_register (&actions[i]);
}
void e2_terminal_options_register (void)
{
    gchar *commands = g_strconcat (_C(6), ":", _C(26), NULL);
    e2_option_bool_register ("terminal-per-tab", commands,
        _("keep output and terminals with file-pane tabs"),
        _("Give each file-pane tab its own application output and terminals. Disabling this combines existing outputs and terminals."),
        "pane-tabs", FALSE, E2_OPTION_FLAG_BASIC | E2_OPTION_FLAG_BUILDPANES | E2_OPTION_FLAG_FREEGROUP);
    gchar *group = g_strconcat (_C(6), ".", _("terminal"), NULL);
    E2_OptionFlags flags = E2_OPTION_FLAG_BASIC | E2_OPTION_FLAG_COMPACT;
    e2_option_str_register ("terminal-shell", group, _("shell executable"),
        _("Executable path only; empty uses SHELL or /bin/sh. New sessions start in the active local pane."),
        NULL, "", flags | E2_OPTION_FLAG_FREEGROUP);
    e2_option_bool_register ("terminal-shell-integration", group, _("report Bash prompt locations"),
        _("Opt in to current user and folder reports from Bash. Preserves your prompt and startup files; applies to new sessions."),
        NULL, FALSE, flags);
    e2_option_int_register ("terminal-scrollback", group, _("scrollback lines"),
        NULL, NULL, 10000, 0, 1000000, flags);
    e2_option_font_register ("terminal-font", group, _("terminal font"),
        _("Terminal preferences apply to new or restarted sessions"), NULL, "Monospace 10", flags);
    e2_option_color_register ("terminal-foreground", group, _("terminal foreground"),
        NULL, NULL, "#dddddd", flags);
    e2_option_color_register ("terminal-background", group, _("terminal background"),
        NULL, NULL, "#202020", flags);
}
#endif
