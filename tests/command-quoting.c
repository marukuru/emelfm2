/* Small adapter for comparison of the production parser with Bash. */
#include "emelfm2.h"
#include "e2_command_quoting.h"
#include "e2_utf8.h"
#include "e2_utils.h"

gchar *(*e2_fname_dupto_locale) (const gchar *) = g_strdup;
int main (int argc, char **argv)
{
	if (argc != 3) return 2;
	if (!strcmp (argv[1], "separator"))
	{
		gchar *found = e2_utils_bare_strchr (argv[2], ';');
		printf ("%ld", found == NULL ? -1L : (long)(found - argv[2]));
		return 0;
	}
	E2_CommandQuotes *quotes = NULL;
	GError *error = NULL;
	gchar *protected = e2_command_quotes_protect (argv[2], &quotes, &error);
	if (protected == NULL)
	{
		g_printerr ("%s\n", error->message);
		g_error_free (error);
		e2_command_quotes_free (quotes);
		return 1;
	}
	if (!strcmp (argv[1], "shell"))
	{
		gchar *text = e2_command_quotes_restore (quotes, protected, E2_COMMAND_QUOTE_SHELL);
		fwrite (text, 1, strlen (text), stdout);
		g_free (text);
	}
	else
	{
		gchar **args;
		if (!g_shell_parse_argv (protected, NULL, &args, &error))
		{
			g_printerr ("%s\n", error->message);
			g_error_free (error);
			g_free (protected);
			e2_command_quotes_free (quotes);
			return 1;
		}
		for (guint i = 0; args[i] != NULL; i++)
		{
			gchar *value = e2_command_quotes_restore (quotes, args[i], E2_COMMAND_QUOTE_ARGUMENT);
			fwrite (value, 1, strlen (value) + 1, stdout);
			g_free (value);
		}
		g_strfreev (args);
	}
	gchar *original = e2_command_quotes_restore (quotes, protected, E2_COMMAND_QUOTE_ORIGINAL);
	g_assert_cmpstr (original, ==, argv[2]);
	g_free (original);
	g_free (protected);
	e2_command_quotes_free (quotes);
	return 0;
}
