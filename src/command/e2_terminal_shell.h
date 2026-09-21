/* Opt-in Bash metadata. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef E2_TERMINAL_SHELL_H
#define E2_TERMINAL_SHELL_H
#include <glib.h>
/* Caller unlinks and frees the private rc file after the session is destroyed. */
gchar *e2_terminal_shell_rc (const gchar *user_rc, GError **error);
#endif
