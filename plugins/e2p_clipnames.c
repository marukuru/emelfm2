/* $Id: e2p_clipnames.c 2865 2013-10-27 09:18:13Z tpgww $

Copyright (C) 2003-2017 tooar <tooar@emelfm2.net>
Portions copyright (C) 1999 Michael Clark

This file is part of emelFM2.
emelFM2 is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

emelFM2 is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with emelFM2; see the file GPL. If not, see http://www.gnu.org/licenses.
*/

/**
@file plugins/e2p_names_clip.c
@brief plugin to copy name and perhaps path of each selected item to the clipboard
*/

#include "emelfm2.h"
#include <string.h>
#include "e2_plugins.h"

//signature component, must match 'core' of this file name and likewise for corresponding icon file name
#define ANAME "clipnames"

static PluginIface iface;

/* Bash ANSI-C quoting keeps every path on one physical line, even if a
 * filename contains a newline. Escape invalid UTF-8 bytes too, since the
 * clipboard must contain valid UTF-8 while Bash needs the original bytes. */
static gchar *_e2p_quote_bash_path (const gchar *path)
{
	gboolean utf8 = g_utf8_validate (path, -1, NULL);
	GString *quoted = g_string_new ("$'");
	const guchar *p;
	for (p = (const guchar *)path; *p != '\0'; p++)
	{
		switch (*p)
		{
			case '\\': g_string_append (quoted, "\\\\"); break;
			case '\'': g_string_append (quoted, "\\'"); break;
			case '\n': g_string_append (quoted, "\\n"); break;
			case '\r': g_string_append (quoted, "\\r"); break;
			case '\t': g_string_append (quoted, "\\t"); break;
			default:
				if (*p < 0x20 || *p == 0x7f || (!utf8 && *p >= 0x80))
					g_string_append_printf (quoted, "\\%03o", (guint)*p);
				else
					g_string_append_c (quoted, *p);
		}
	}
	g_string_append_c (quoted, '\'');
	return g_string_free (quoted, FALSE);
}

static gboolean _e2p_copy_bash_paths (gboolean lined)
{
	/* Use the actual selection, not a macro-expanded string: quotes and
	 * whitespace inside filenames must not be mistaken for separators. */
	GPtrArray *selected = e2_fileview_get_selected (curr_view);
	if (selected == NULL)
		return FALSE;
	gchar *directory = F_FILENAME_TO_LOCALE (curr_view->dir);
	GString *text = g_string_new ("");
	guint i;
	for (i = 0; i < selected->len; i++)
	{
		E2_SelectedItemInfo *info = g_ptr_array_index (selected, i);
		gchar *path = g_build_filename (directory, info->filename, NULL);
		gchar *quoted = _e2p_quote_bash_path (path);
		if (i > 0) g_string_append_c (text, lined ? '\n' : ' ');
		g_string_append (text, quoted);
		g_free (quoted);
		g_free (path);
	}
	F_FREE (directory, curr_view->dir);
	e2_fileview_clean_selected (selected);
	gtk_clipboard_set_text (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD), text->str, text->len);
	g_string_free (text, TRUE);
	return TRUE;
}

/**
@brief copy name and perhaps path of each selected item to the clipboard

@param from the button, menu item etc which was activated
@param art action runtime data

@return TRUE if action completed successfully, else FALSE
*/
static gboolean _e2p_copy_to_clipboard (gpointer from, E2_ActionRuntime *art)
{
	/* These explicit formats are independent of the current modifier keys. */
	if (art->data != NULL && !strcmp (art->data, "bash"))
		return _e2p_copy_bash_paths (FALSE);
	if (art->data != NULL && !strcmp (art->data, "bash-lines"))
		return _e2p_copy_bash_paths (TRUE);
	GString *text;
	gchar *names;
	gboolean full =
			(ACTION_BUTTON (art,1) && ACTION_MASK (art,GDK_SHIFT_MASK))
		 || (art->data != NULL && strstr ((gchar*)art->data, _("shift")) != NULL);
	gboolean lined =
			(ACTION_BUTTON (art,1) && ACTION_MASK (art,GDK_CONTROL_MASK))
		 || (art->data != NULL && strstr ((gchar*)art->data, _("ctrl")) != NULL);
	//art was probably packed during context-menu creation - which is too early
	//for pressing mod-keys
	if ((!full || !lined) /*&& (GTK_IS_BUTTON (from) || GTK_IS_MENU_ITEM (from))*/)
	{
		GdkModifierType state = art->state; //can ignore the custom button-number
		full = full || (state & GDK_SHIFT_MASK);
		lined = lined || (state & GDK_CONTROL_MASK);
	}

	if (full || lined)
	{
		//get quoted list of selected-item-names, to facilitate their separation
		names = e2_utils_expand_macros ("%f", NULL);
		if (names == NULL)
			return FALSE;

		gchar sep = (lined) ? '\n' : ' ';

		gchar **split = g_strsplit (names, "\"", -1);
		gchar **tmp = split;
		text = g_string_new ("");
		while (*tmp != NULL)
		{
			if (**tmp == '\0')	//empty string before 1st " and after last "
			{
				tmp++;
				continue;
			}
			else if (**tmp == ' ')
				text = g_string_append_c (text, sep);
			else
			{
				if (full)
//E2_VFSTMPOK
					text = g_string_append (text, curr_view->dir);
				text = g_string_append (text, *tmp);
			}
			tmp++;
		}
		g_free (names);
		g_strfreev (split);
	}
	else
	{
		//get un-quoted list of selected item names
		names = e2_utils_expand_macros ("%%f", NULL);
		if (names == NULL)
			return FALSE;
		text = g_string_new (names);
	}

	GtkClipboard *clip = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);
  	gtk_clipboard_set_text (clip, text->str, text->len);
	g_string_free (text, TRUE);
	return TRUE;
}

/**
@brief plugin initialization function, called by main program

@param mode flags enumerating what sort of init to perform

@return Plugin*, with refcount 1 if @a mode included runtime setup and that succeeded
*/
Plugin *init_plugin (E2PInit mode)
{
	PLUGINIT_ONEACTION_SIMPLE (_A(6),_("copy_name"),_e2p_copy_to_clipboard,
		_("Copy _names"),
		_("Copy path or name of each selected item to the clipboard"),
		"plugin-clip"E2ICONTB)
}
/**
@brief cleanup transient things for this plugin

@param p pointer to data struct for the plugin

@return TRUE if all cleanups were completed
*/
gboolean clean_plugin (Plugin *p)
{
	PLUGIN_CLEAR_ACTIONS (p)
	return ret;
}
