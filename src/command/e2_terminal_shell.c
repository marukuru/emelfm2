/* Opt-in Bash metadata. SPDX-License-Identifier: GPL-3.0-or-later */
#include "e2_terminal_shell.h"
#include <glib/gstdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

/* Bash reads the user's normal rc file first. Append to scalar or array prompt
 * hooks without changing PS1, traps, history settings or the user's files.
 * No preexec trap: process existence is not evidence that a command is busy. */
static const gchar hook[] =
    "\n__emelfm2_location() {\n"
    "    local e2_status=$? e2_user e2_host e2_path e2_uri= e2_char e2_hex e2_i\n"
    "    e2_user=$(command id -un) || return \"$e2_status\"\n"
    "    e2_host=${HOSTNAME:-localhost}\n"
    "    e2_path=$PWD\n"
    "    printf '\\033]0;%s@%s:%s\\007' \"${e2_user//[[:cntrl:]]/}\" \"${e2_host//[[:cntrl:]]/}\" \"${e2_path//[[:cntrl:]]/}\"\n"
    "    local LC_ALL=C\n"
    "    for ((e2_i=0; e2_i<${#e2_path}; e2_i++)); do\n"
    "        e2_char=${e2_path:e2_i:1}\n"
    "        case $e2_char in\n"
    "            [a-zA-Z0-9/_.~-]) e2_uri+=$e2_char ;;\n"
    "            *) printf -v e2_hex '%%%02X' \"'$e2_char\"; e2_uri+=$e2_hex ;;\n"
    "        esac\n"
    "    done\n"
    "    printf '\\033]7;file://%s%s\\007' \"${e2_host//[!a-zA-Z0-9.-]/}\" \"$e2_uri\"\n"
    "    return \"$e2_status\"\n"
    "}\n"
    "PROMPT_COMMAND+=(__emelfm2_location)\n";

gchar *e2_terminal_shell_rc (const gchar *user_rc, GError **error)
{
    gchar *path = NULL;
    gint fd = g_file_open_tmp ("emelfm2-bash-XXXXXX", &path, error);
    if (fd < 0) return NULL;
    gchar *quoted = g_shell_quote (user_rc);
    gchar *script = g_strdup_printf ("if [[ -r %s ]]; then source %s; fi\n%s", quoted, quoted, hook);
    g_free (quoted);
    const gchar *cursor = script;
    gsize remaining = strlen (script);
    while (remaining > 0)
    {
        ssize_t count = write (fd, cursor, remaining);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0)
        {
            g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                "Cannot write shell integration: %s", g_strerror (errno));
            close (fd);
            g_unlink (path);
            g_free (path); g_free (script);
            return NULL;
        }
        cursor += count;
        remaining -= count;
    }
    g_free (script);
    if (!g_close (fd, error)) { g_unlink (path); g_free (path); return NULL; }
    return path;
}
