/* Regression tests for wildcard expansion followed by command parsing.
 * The directory adapter below runs the production wildcard callbacks against
 * real temporary files, without starting the file manager's UI threads. */
#include "emelfm2.h"
#include "e2_utils.h"
#include "e2_fs.h"
#include <glib/gstdio.h>

static ViewInfo view;
ViewInfo *curr_view = &view;
pthread_mutex_t display_mutex = PTHREAD_MUTEX_INITIALIZER;
gchar *(*e2_fname_from_locale) (const gchar *) = g_strdup;
gchar *(*e2_fname_to_locale) (const gchar *) = g_strdup;
gchar *(*e2_fname_dupfrom_locale) (const gchar *) = g_strdup;
gchar *(*e2_fname_dupto_locale) (const gchar *) = g_strdup;

void e2_utf8_fname_free (gchar *converted, const gchar *original)
{
	if (converted != original)
		g_free (converted);
}

void e2_main_open_uilock (void) {}
void e2_main_close_uilock (void) {}

gint e2_fs_stat (VPATH *path, struct stat *buf)
{
	return stat (path, buf);
}

gboolean e2_fs_is_dir3 (VPATH *path)
{
	return g_file_test (path, G_FILE_TEST_IS_DIR);
}

gpointer e2_fs_dir_foreach (VPATH *path, E2_FsReadWatch monitor,
	gpointer filterfunc, gpointer data, GDestroyNotify destroy)
{
	gboolean (*filter) (VPATH *, const gchar *, GList **, gpointer) = filterfunc;
	GDir *dir = g_dir_open (path, 0, NULL);
	g_assert_nonnull (dir);
	GList *matches = NULL;
	const gchar *name;
	while ((name = g_dir_read_name (dir)) != NULL)
		if (!filter (path, name, &matches, data))
			break;
	g_dir_close (dir);
	if (destroy != NULL)
		destroy (data);
	return matches;
}

static gchar **parse (const gchar *command)
{
	gchar *raw = g_strdup (command);
	gchar *expanded = e2_utils_replace_wildcards (raw);
	gchar **argv = NULL;
	GError *error = NULL;
	g_assert_true (g_shell_parse_argv (expanded, NULL, &argv, &error));
	g_assert_no_error (error);
	if (expanded != raw)
		g_free (expanded);
	g_free (raw);
	return argv;
}

static const gchar *names[] = {
	"plain.png", "with space.png", "apostrophe's.png", "double\"quote.png",
	"back\\slash.png", "tab\tname.png", "line\nbreak.png", "#comment.png",
	"semi;colon.png", "amp&ersand.png", "pipe|name.png", "$(literal).png",
	"`literal`.png", "star*.png", "question?.png", "bracket[1].png",
	"caf\303\251.png", "both'\"quotes.png", "trailing-slash\\.png",
	"\\literal.png", "-option.png"
};

static void check_matches (const gchar *command, const gchar *prefix)
{
	gchar **argv = parse (command);
	g_assert_cmpstr (argv[0], ==, "capture");
	g_assert_cmpuint (g_strv_length (argv), ==, G_N_ELEMENTS (names) + 1);
	for (guint i = 0; i < G_N_ELEMENTS (names); i++)
	{
		gchar *expected = g_strconcat (prefix, names[i], NULL);
		guint count = 0;
		for (guint j = 1; argv[j] != NULL; j++)
			if (!strcmp (argv[j], expected))
				count++;
		g_assert_cmpuint (count, ==, 1);
		g_free (expected);
	}
	g_strfreev (argv);
}

static void test_names (void)
{
	check_matches ("capture *.png", "");
	check_matches ("capture ./*.png", "./");
}

static void test_directory (void)
{
	gchar *prefix = g_strconcat (view.dir, "dir'\"\\\t\n&/", NULL);
	g_assert_cmpint (g_mkdir (prefix, 0700), ==, 0);
	for (guint i = 0; i < G_N_ELEMENTS (names); i++)
	{
		gchar *path = g_strconcat (prefix, names[i], NULL);
		g_assert_true (g_file_set_contents (path, "", 0, NULL));
		g_free (path);
	}
	check_matches ("capture dir*/*.png", prefix);
	check_matches ("capture dir??????" "/*.png", prefix);
	gchar **argv = parse ("capture dir*/plain.png");
	gchar *expected = g_strconcat (prefix, "plain.png", NULL);
	g_assert_cmpuint (g_strv_length (argv), ==, 2);
	g_assert_cmpstr (argv[1], ==, expected);
	g_free (expected);
	g_strfreev (argv);
	argv = parse ("capture dir*/plai?.png");
	expected = g_strconcat (prefix, "plain.png", NULL);
	g_assert_cmpuint (g_strv_length (argv), ==, 2);
	g_assert_cmpstr (argv[1], ==, expected);
	g_free (expected);
	g_strfreev (argv);
	for (guint i = 0; i < G_N_ELEMENTS (names); i++)
	{
		gchar *path = g_strconcat (prefix, names[i], NULL);
		g_assert_cmpint (g_remove (path), ==, 0);
		g_free (path);
	}
	g_assert_cmpint (g_rmdir (prefix), ==, 0);
	g_free (prefix);
}

static void test_literals (void)
{
	gchar **argv = parse ("capture '*.png' \"*.png\" no-match-*.xyz \\*.png \\?.png");
	g_assert_cmpuint (g_strv_length (argv), ==, 6);
	g_assert_cmpstr (argv[1], ==, "*.png");
	g_assert_cmpstr (argv[2], ==, "*.png");
	g_assert_cmpstr (argv[3], ==, "no-match-*.xyz");
	g_assert_cmpstr (argv[4], ==, "*.png");
	g_assert_cmpstr (argv[5], ==, "?.png");
	g_strfreev (argv);
}

static void test_tokenization (void)
{
	gchar **argv = parse ("capture \"don't expand *.png\" two\\ words\t*.png\n'last'");
	g_assert_cmpuint (g_strv_length (argv), ==, G_N_ELEMENTS (names) + 4);
	g_assert_cmpstr (argv[1], ==, "don't expand *.png");
	g_assert_cmpstr (argv[2], ==, "two words");
	g_assert_cmpstr (argv[G_N_ELEMENTS (names) + 3], ==, "last");
	g_strfreev (argv);
}

static void test_execution (void)
{
	gchar *directory = g_strconcat (view.dir, "delete", NULL);
	g_assert_cmpint (g_mkdir (directory, 0700), ==, 0);
	for (guint i = 0; i < G_N_ELEMENTS (names); i++)
	{
		gchar *path = g_build_filename (directory, names[i], NULL);
		g_assert_true (g_file_set_contents (path, "", 0, NULL));
		g_free (path);
	}
	gchar *keeper = g_build_filename (directory, "keep.txt", NULL);
	g_assert_true (g_file_set_contents (keeper, "keep", -1, NULL));
	gchar **argv = parse ("rm -- delete/*.png");
	GError *error = NULL;
	gint status;
	g_assert_true (g_spawn_sync (view.dir, argv, NULL, G_SPAWN_SEARCH_PATH,
		NULL, NULL, NULL, NULL, &status, &error));
	g_assert_no_error (error);
	g_assert_cmpint (status, ==, 0);
	g_assert_true (g_file_test (keeper, G_FILE_TEST_IS_REGULAR));
	for (guint i = 0; i < G_N_ELEMENTS (names); i++)
	{
		gchar *path = g_build_filename (directory, names[i], NULL);
		g_assert_false (g_file_test (path, G_FILE_TEST_EXISTS));
		g_free (path);
	}
	g_assert_cmpint (g_remove (keeper), ==, 0);
	g_assert_cmpint (g_rmdir (directory), ==, 0);
	g_free (keeper);
	g_free (directory);
	g_strfreev (argv);
}

int main (int argc, char **argv)
{
	g_test_init (&argc, &argv, NULL);
	/* Release builds call the mutex directly; match the application's recursive
	 * UI lock instead of the non-recursive static test initializer. */
	pthread_mutexattr_t attr;
	pthread_mutexattr_init (&attr);
	pthread_mutexattr_settype (&attr, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_destroy (&display_mutex);
	pthread_mutex_init (&display_mutex, &attr);
	pthread_mutexattr_destroy (&attr);
	gchar *directory = g_dir_make_tmp ("emelfm2-wildcards-XXXXXX", NULL);
	g_assert_nonnull (directory);
	g_strlcpy (view.dir, directory, sizeof (view.dir));
	g_strlcat (view.dir, "/", sizeof (view.dir));
	for (guint i = 0; i < G_N_ELEMENTS (names); i++)
	{
		gchar *path = g_build_filename (directory, names[i], NULL);
		g_assert_true (g_file_set_contents (path, "", 0, NULL));
		g_free (path);
	}
	gchar *unrelated = g_build_filename (directory, "keep.txt", NULL);
	g_assert_true (g_file_set_contents (unrelated, "", 0, NULL));
	g_test_add_func ("/command/wildcards/filenames", test_names);
	g_test_add_func ("/command/wildcards/directories", test_directory);
	g_test_add_func ("/command/wildcards/literals", test_literals);
	g_test_add_func ("/command/wildcards/tokenization", test_tokenization);
	g_test_add_func ("/command/wildcards/execution", test_execution);
	pthread_mutex_lock (&display_mutex);
	int result = g_test_run ();
	pthread_mutex_unlock (&display_mutex);
	for (guint i = 0; i < G_N_ELEMENTS (names); i++)
	{
		gchar *path = g_build_filename (directory, names[i], NULL);
		g_assert_cmpint (g_remove (path), ==, 0);
		g_free (path);
	}
	g_assert_cmpint (g_remove (unrelated), ==, 0);
	g_free (unrelated);
	g_assert_cmpint (g_rmdir (directory), ==, 0);
	g_free (directory);
	return result;
}
