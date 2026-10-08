/* ANSI-C quoted literals in the internal command interpreter. */
#ifndef __E2_COMMAND_QUOTING_H__
#define __E2_COMMAND_QUOTING_H__
#include <glib.h>

typedef struct _E2_CommandQuotes E2_CommandQuotes;
typedef enum
{
	E2_COMMAND_QUOTE_ORIGINAL, /* restore UTF-8 source for logs and actions */
	E2_COMMAND_QUOTE_SHELL,    /* native bytes, safely quoted for a shell */
	E2_COMMAND_QUOTE_ARGUMENT  /* native bytes in an already parsed argument */
} E2_CommandQuoteMode;

/* Replace top-level $'...' literals with quoted placeholders while expanding
 * macros, variables and wildcards. Can append literals to an existing context. */
gchar *e2_command_quotes_protect (const gchar *text, E2_CommandQuotes **quotes,
	GError **error);
gchar *e2_command_quotes_restore (E2_CommandQuotes *quotes, const gchar *text,
	E2_CommandQuoteMode mode);
void e2_command_quotes_free (E2_CommandQuotes *quotes);
#endif
