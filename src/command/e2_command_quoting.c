/* Keep ANSI-C quoted filename bytes literal throughout command expansion.
 * Released under the GNU General Public License, version 3 or later. */
#include "emelfm2.h"
#include "e2_command_quoting.h"
#include "e2_utf8.h"

struct _E2_CommandQuotes
{
	gchar *prefix;
	GPtrArray *source;
	GPtrArray *bytes;
};

static void append_native (GString *out, const gchar *start, gsize length)
{
	gchar *text = g_strndup (start, length);
	gchar *native = D_FILENAME_TO_LOCALE (text);
	g_string_append (out, native);
	g_free (native);
	g_free (text);
}

/* Bash stops the current quoted literal at an escaped NUL, but still consumes
 * its remaining source. Filenames and exec arguments cannot contain NUL. */
static gchar *decode (const gchar *start, const gchar **end, GError **error)
{
	GString *out = g_string_new (NULL);
	const gchar *p = start + 2;
	gboolean nul = FALSE;
	while (*p != '\0' && *p != '\'')
	{
		if (*p != '\\')
		{
			const gchar *literal = p;
			while (*p != '\0' && *p != '\'' && *p != '\\') p++;
			if (!nul) append_native (out, literal, p - literal);
			continue;
		}
		p++;
		if (*p == '\0') break;
		guint value;
		switch (*p++)
		{
			case 'a': value = '\a'; break;
			case 'b': value = '\b'; break;
			case 'e': case 'E': value = 27; break;
			case 'f': value = '\f'; break;
			case 'n': value = '\n'; break;
			case 'r': value = '\r'; break;
			case 't': value = '\t'; break;
			case 'v': value = '\v'; break;
			case '\\': case '\'': case '"': case '?': value = p[-1]; break;
			case 'c':
				if (*p == '\0') goto unknown;
				value = *p == '?' ? 127 : ((guchar)g_ascii_toupper (*p) & 31);
				p++;
				break;
			case 'x': case 'u': case 'U':
			{
				gchar kind = p[-1];
				guint max = kind == 'x' ? 2 : kind == 'u' ? 4 : 8;
				if (!g_ascii_isxdigit (*p)) goto unknown;
				value = 0;
				for (guint i = 0; i < max && g_ascii_isxdigit (*p); i++, p++)
					value = (value << 4) | g_ascii_xdigit_value (*p);
				if (kind != 'x' && value != 0)
				{
					if (!g_unichar_validate (value))
					{
						g_set_error_literal (error, G_SHELL_ERROR, G_SHELL_ERROR_BAD_QUOTING,
							_("Invalid Unicode escape in ANSI-C quoted string"));
						g_string_free (out, TRUE);
						return NULL;
					}
					gchar utf[6];
					gint length = g_unichar_to_utf8 (value, utf);
					if (!nul) append_native (out, utf, length);
					continue;
				}
				break;
			}
			default:
				if (p[-1] >= '0' && p[-1] <= '7')
				{
					value = p[-1] - '0';
					for (guint i = 1; i < 3 && *p >= '0' && *p <= '7'; i++, p++)
						value = (value << 3) | (*p - '0');
					value &= 255;
					break;
				}
unknown:
				if (!nul) g_string_append_c (out, '\\');
				value = (guchar)p[-1];
				break;
		}
		if (value == 0) nul = TRUE;
		if (!nul) g_string_append_c (out, value);
	}
	if (*p != '\'')
	{
		g_set_error_literal (error, G_SHELL_ERROR, G_SHELL_ERROR_BAD_QUOTING,
			_("Unterminated ANSI-C quoted string"));
		g_string_free (out, TRUE);
		return NULL;
	}
	*end = p + 1;
	return g_string_free (out, FALSE);
}

gchar *e2_command_quotes_protect (const gchar *text, E2_CommandQuotes **quotes,
	GError **error)
{
	GString *out = g_string_new (NULL);
	gchar quote = '\0';
	for (const gchar *p = text; *p != '\0'; )
	{
		if (quote == '\0' && p[0] == '$' && p[1] == '\'')
		{
			const gchar *end;
			gchar *bytes = decode (p, &end, error);
			if (bytes == NULL) { g_string_free (out, TRUE); return NULL; }
			if (*quotes == NULL)
			{
				*quotes = g_new0 (E2_CommandQuotes, 1);
				do
				{
					g_free ((*quotes)->prefix);
					(*quotes)->prefix = g_strdup_printf ("__e2_ansi_%08x%08x_",
						g_random_int (), g_random_int ());
				} while (strstr (text, (*quotes)->prefix) != NULL);
				(*quotes)->source = g_ptr_array_new_with_free_func (g_free);
				(*quotes)->bytes = g_ptr_array_new_with_free_func (g_free);
			}
			g_string_append_printf (out, "'%s%u_'", (*quotes)->prefix, (*quotes)->source->len);
			g_ptr_array_add ((*quotes)->source, g_strndup (p, end - p));
			g_ptr_array_add ((*quotes)->bytes, bytes);
			p = end;
		}
		else if (*p == '\\' && quote != '\'' && p[1] != '\0')
		{
			g_string_append_len (out, p, 2);
			p += 2;
		}
		else
		{
			if (quote != '\0') { if (*p == quote) quote = '\0'; }
			else if (*p == '\'' || *p == '"') quote = *p;
			g_string_append_c (out, *p++);
		}
	}
	return g_string_free (out, FALSE);
}

gchar *e2_command_quotes_restore (E2_CommandQuotes *quotes, const gchar *text,
	E2_CommandQuoteMode mode)
{
	if (quotes == NULL)
		return mode == E2_COMMAND_QUOTE_ORIGINAL ? g_strdup (text) : D_FILENAME_TO_LOCALE (text);
	gboolean parsed = mode == E2_COMMAND_QUOTE_ARGUMENT;
	gchar *prefix = g_strconcat (parsed ? "" : "'", quotes->prefix, NULL);
	GString *out = g_string_new (NULL);
	const gchar *p = text, *match;
	while ((match = strstr (p, prefix)) != NULL)
	{
		gchar *end;
		guint64 index = g_ascii_strtoull (match + strlen (prefix), &end, 10);
		if (end == match + strlen (prefix) || *end != '_'
			|| (!parsed && end[1] != '\'') || index >= quotes->source->len)
			break;
		if (mode == E2_COMMAND_QUOTE_ORIGINAL) g_string_append_len (out, p, match - p);
		else append_native (out, p, match - p);
		if (mode == E2_COMMAND_QUOTE_ORIGINAL)
			g_string_append (out, g_ptr_array_index (quotes->source, index));
		else if (parsed)
			g_string_append (out, g_ptr_array_index (quotes->bytes, index));
		else
		{
			gchar *literal = g_shell_quote (g_ptr_array_index (quotes->bytes, index));
			g_string_append (out, literal);
			g_free (literal);
		}
		p = end + (parsed ? 1 : 2);
	}
	if (mode == E2_COMMAND_QUOTE_ORIGINAL) g_string_append (out, p);
	else append_native (out, p, strlen (p));
	g_free (prefix);
	return g_string_free (out, FALSE);
}

void e2_command_quotes_free (E2_CommandQuotes *quotes)
{
	if (quotes == NULL) return;
	g_free (quotes->prefix);
	g_ptr_array_free (quotes->source, TRUE);
	g_ptr_array_free (quotes->bytes, TRUE);
	g_free (quotes);
}
