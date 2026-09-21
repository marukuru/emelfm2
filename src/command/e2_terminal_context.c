/* Terminal identity tracking. SPDX-License-Identifier: GPL-3.0-or-later */
#include "e2_terminal_context.h"
#include <pwd.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>

gboolean e2_terminal_context_has_jobs (GPid shell, GPid foreground, const gchar *executable)
{
    if (shell <= 0) return FALSE;
    if (foreground > 0 && foreground != shell) return TRUE;
#ifdef __linux__
    /* A foreground check alone misses background and stopped shell jobs. */
    gchar *path = g_strdup_printf ("/proc/%ld/task/%ld/children", (long) shell, (long) shell);
    gchar *children = NULL;
    g_file_get_contents (path, &children, NULL, NULL);
    g_free (path);
    gboolean busy = FALSE;
    for (gchar *p = children; p != NULL && *p != '\0' && !busy; )
    {
        gchar *end;
        long child = strtol (p, &end, 10);
        if (p == end || child <= 0) break;
        p = end;
        path = g_strdup_printf ("/proc/%ld/stat", child);
        gchar *record = NULL;
        if (g_file_get_contents (path, &record, NULL, NULL))
        {
            gchar *fields = strrchr (record, ')');
            gchar state;
            busy = fields != NULL && sscanf (fields + 1, " %c", &state) == 1
                && state != 'Z' && state != 'X';
            g_free (record);
        }
        g_free (path);
    }
    g_free (children);
    if (busy) return TRUE;

    /* `exec program` replaces the shell without changing its PID or group. */
    gchar *command = executable == NULL ? NULL : g_find_program_in_path (executable);
    path = g_strdup_printf ("/proc/%ld/exe", (long) shell);
    struct stat running, original;
    busy = command != NULL && stat (path, &running) == 0 && stat (command, &original) == 0
        && (running.st_dev != original.st_dev || running.st_ino != original.st_ino);
    g_free (command);
    g_free (path);
    return busy;
#else
    return FALSE;
#endif
}

struct _E2_TerminalContext
{
    GPid pid;
    uid_t uid;
    gchar *user, *directory, *process_directory;
    gchar *title, *uri, *reported_user, *reported_directory;
    gchar *reported_host, *uri_host;
    gboolean reported_local, remote_process;
};

static gchar *user_name (uid_t uid)
{
    struct passwd record, *result = NULL;
    gsize size = 1024;
    gchar *buffer = NULL;
    gint error;
    do
    {
        size *= 2;
        buffer = g_realloc (buffer, size);
        error = getpwuid_r (uid, &record, buffer, size, &result);
    } while (error == ERANGE && size < 1024 * 1024);
    gchar *name = (error == 0 && result != NULL) ?
        g_strdup (result->pw_name) : g_strdup_printf ("%lu", (unsigned long) uid);
    g_free (buffer);
    return name;
}

E2_TerminalContext *e2_terminal_context_new (const gchar *directory)
{
    E2_TerminalContext *context = g_new0 (E2_TerminalContext, 1);
    context->uid = geteuid ();
    context->user = user_name (context->uid);
    context->directory = g_strdup (directory);
    return context;
}

void e2_terminal_context_free (E2_TerminalContext *context)
{
    g_free (context->user);
    g_free (context->directory);
    g_free (context->process_directory);
    g_free (context->title);
    g_free (context->uri);
    g_free (context->reported_user);
    g_free (context->reported_directory);
    g_free (context->reported_host);
    g_free (context->uri_host);
    g_free (context);
}

static void clear_report (E2_TerminalContext *context)
{
    g_free (context->reported_user);
    g_free (context->reported_directory);
    context->reported_user = context->reported_directory = NULL;
    g_free (context->reported_host);
    g_free (context->uri_host);
    context->reported_host = context->uri_host = NULL;
}

static gboolean local_host (const gchar *host)
{
    if (host == NULL || *host == '\0' || !strcmp (host, "localhost")) return TRUE;
    const gchar *local = g_get_host_name (), *dot = strchr (local, '.');
    return !g_ascii_strcasecmp (host, local) || (dot != NULL
        && strlen (host) == (gsize)(dot - local) && !g_ascii_strncasecmp (host, local, dot - local));
}

/* Recognize the conventional shell title user@host:/path (also :~/path).
 * An editor's document title or other arbitrary application title is ignored. */
static void read_title (E2_TerminalContext *context, const gchar *title)
{
    if (title == NULL || !g_utf8_validate (title, -1, NULL)) return;
    const gchar *at = strchr (title, '@');
    const gchar *colon = at == NULL ? NULL : strchr (at + 1, ':');
    if (at == NULL || at == title || colon == NULL || colon == at + 1
        || (colon[1] != '/' && colon[1] != '~')) return;
    for (const gchar *p = title; p < at; p = g_utf8_next_char (p))
        if (g_unichar_isspace (g_utf8_get_char (p)) || g_unichar_iscntrl (g_utf8_get_char (p))) return;
    clear_report (context);
    context->reported_user = g_strndup (title, at - title);
    context->reported_directory = g_strdup (colon + 1);
    context->reported_host = g_strndup (at + 1, colon - at - 1);
    context->reported_local = local_host (context->reported_host);
}

#ifdef __linux__
/* sudo and similar wrappers can proxy a second PTY. Follow their process tree
 * to that PTY's foreground group, rather than displaying the proxy's user/cwd.
 * Limit traversal, and do not follow background jobs of ordinary shells. */
static GPid inner_foreground (GPid pid, guint *remaining)
{
    if (*remaining == 0) return pid;
    (*remaining)--;
    GPid found = pid;
    gchar *path = g_strdup_printf ("/proc/%ld/task/%ld/children", (long) pid, (long) pid);
    gchar *children = NULL;
    g_file_get_contents (path, &children, NULL, NULL);
    g_free (path);
    for (gchar *p = children; p != NULL && *p != '\0' && *remaining > 0; )
    {
        gchar *end;
        long child = strtol (p, &end, 10);
        if (p == end || child <= 0) break;
        p = end;
        path = g_strdup_printf ("/proc/%ld/stat", child);
        gchar *record = NULL;
        if (g_file_get_contents (path, &record, NULL, NULL))
        {
            gchar *fields = strrchr (record, ')');
            gchar state;
            long parent, group, session, tty, foreground;
            if (fields != NULL && sscanf (fields + 1, " %c %ld %ld %ld %ld %ld",
                &state, &parent, &group, &session, &tty, &foreground) == 6
                && tty != 0 && group == foreground && state != 'Z')
                found = (GPid) child;
            g_free (record);
        }
        g_free (path);
        GPid nested = inner_foreground ((GPid) child, remaining);
        if (nested != child) found = nested;
    }
    g_free (children);
    return found;
}

static GPid context_process (GPid foreground)
{
    gchar *path = g_strdup_printf ("/proc/%ld/comm", (long) foreground), *name = NULL;
    g_file_get_contents (path, &name, NULL, NULL);
    g_free (path);
    if (name != NULL)
    {
        g_strchomp (name);
        if (!strcmp (name, "sudo") || !strcmp (name, "su") || !strcmp (name, "doas")
            || !strcmp (name, "pkexec") || !strcmp (name, "script"))
        {
            guint remaining = 32;
            foreground = inner_foreground (foreground, &remaining);
        }
        g_free (name);
    }
    return foreground;
}
#endif

void e2_terminal_context_update (E2_TerminalContext *context, GPid foreground,
    const gchar *title, const gchar *uri)
{
    uid_t uid = context->uid;
    gboolean known_user = FALSE;
    gchar *directory = NULL;
#ifdef __linux__
    if (foreground > 0)
    {
        foreground = context_process (foreground);
        gchar *comm_path = g_strdup_printf ("/proc/%ld/comm", (long) foreground), *name = NULL;
        context->remote_process = FALSE;
        if (g_file_get_contents (comm_path, &name, NULL, NULL))
        {
            g_strchomp (name);
            context->remote_process = !strcmp (name, "ssh") || !strcmp (name, "mosh-client") || !strcmp (name, "telnet");
            g_free (name);
        }
        g_free (comm_path);
        gchar *path = g_strdup_printf ("/proc/%ld/status", (long) foreground), *status;
        if (g_file_get_contents (path, &status, NULL, NULL))
        {
            gchar *line = strstr (status, "\nUid:");
            unsigned long real, effective;
            if (line != NULL && sscanf (line, "\nUid: %lu %lu", &real, &effective) == 2)
            {
                uid = (uid_t) effective;
                known_user = TRUE;
            }
            g_free (status);
        }
        g_free (path);
        path = g_strdup_printf ("/proc/%ld/cwd", (long) foreground);
        directory = g_file_read_link (path, NULL);
        g_free (path);
    }
#endif
    gboolean changed = foreground != context->pid || (known_user && uid != context->uid)
        || (directory != NULL && g_strcmp0 (directory, context->process_directory) != 0);
    if (changed)
    {
        clear_report (context);
        /* A new foreground process may belong to a different user/host. Do
         * not mislabel an unreadable directory with the previous shell's cwd. */
        if (directory == NULL && ((foreground != context->pid && context->pid > 0)
            || (known_user && uid != context->uid)))
        {
            g_free (context->directory);
            context->directory = g_strdup ("?");
        }
    }
    context->pid = foreground;
    if (known_user && uid != context->uid)
    {
        context->uid = uid;
        g_free (context->user);
        context->user = user_name (uid);
    }
    if (directory != NULL)
    {
        g_free (context->directory);
        context->directory = g_strdup (directory);
    }
    if (directory != NULL || changed)
    {
        g_free (context->process_directory);
        context->process_directory = directory;
    }

    if (g_strcmp0 (title, context->title) != 0)
    {
        g_free (context->title);
        context->title = g_strdup (title);
        read_title (context, title);
    }
    if (g_strcmp0 (uri, context->uri) != 0)
    {
        g_free (context->uri);
        context->uri = g_strdup (uri);
        g_free (context->uri_host);
        context->uri_host = NULL;
        gchar *reported = uri == NULL ? NULL : g_filename_from_uri (uri, &context->uri_host, NULL);
        if (reported != NULL)
        {
            g_free (context->reported_directory);
            context->reported_directory = reported;
        }
    }
}

static gchar *display_text (const gchar *text)
{
    gchar *valid = g_filename_display_name (text);
    GString *clean = g_string_new (NULL);
    for (const gchar *p = valid; *p != '\0'; p = g_utf8_next_char (p))
    {
        gunichar c = g_utf8_get_char (p);
        g_string_append_unichar (clean, g_unichar_iscntrl (c) ? '?' : c);
    }
    g_free (valid);
    return g_string_free (clean, FALSE);
}

gchar *e2_terminal_context_label (E2_TerminalContext *context)
{
    const gchar *user = context->reported_user != NULL ? context->reported_user : context->user;
    const gchar *directory = context->reported_directory;
    if (directory == NULL || (directory[0] == '~' && context->reported_local
        && context->process_directory != NULL
        && g_strcmp0 (user, context->user) == 0)) directory = context->directory;
    gchar *base = g_path_get_basename (directory);
    gchar *folder = display_text (base), *name = display_text (user);
    gchar *label = g_strconcat (name, "@", folder, NULL);
    g_free (base); g_free (folder); g_free (name);
    return label;
}

gchar *e2_terminal_context_description (E2_TerminalContext *context)
{
    const gchar *directory = context->reported_directory != NULL ? context->reported_directory : context->directory;
    const gchar *host = context->uri_host != NULL ? context->uri_host : context->reported_host;
    gchar *path = display_text (directory), *description;
    if (!local_host (host))
    {
        gchar *name = display_text (host);
        description = g_strconcat (name, ":", path, NULL);
        g_free (name);
        g_free (path);
    }
    else description = path;
    return description;
}

gchar *e2_terminal_context_local_directory (E2_TerminalContext *context)
{
    /* A remote report must never turn into a local navigation command, even
     * if a matching path exists on this machine. Use only the observed cwd. */
    if (context->remote_process || !local_host (context->reported_host)
        || !local_host (context->uri_host) || context->process_directory == NULL
        || !g_path_is_absolute (context->process_directory)
        || !g_file_test (context->process_directory, G_FILE_TEST_IS_DIR)) return NULL;
    return g_strdup (context->process_directory);
}
