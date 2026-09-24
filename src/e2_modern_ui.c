/* Optional flat presentation for the existing GTK widgets.
 * Copyright (C) 2026 emelFM2 contributors
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "e2_modern_ui.h"
#include "e2_option.h"
#include <string.h>

#ifdef E2_MODERN_UI
static gboolean enabled, initialized;
static GHashTable *icons;

/* Read once after all configuration/command-line overrides. Applying unrelated
 * options can rebuild the window, but must not partially switch its appearance. */
gboolean e2_modern_ui_enabled (void)
{
	return enabled;
}

/* Do not alter file/text renderers, VTE, window decorations, or their colors.
 * Internal children (combo entries, etc.) are styled after mapping
 * too, as are controls in dialogs and menus created after the main window. */
static gboolean _e2_modern_ui_chrome (GtkWidget *widget)
{
#ifndef USE_GTK3_0
	/* Composite buttons have private theme geometry/painting. In particular,
	 * some dark engines intentionally ignore the app's legacy gray header
	 * override. Keep that appearance, as well as combo arrow allocations. */
	if (GTK_IS_BUTTON (widget) && gtk_widget_get_parent (widget) != NULL
		&& (GTK_IS_COMBO_BOX (gtk_widget_get_parent (widget))
			|| GTK_IS_TREE_VIEW (gtk_widget_get_parent (widget)))) return FALSE;
#endif
	return GTK_IS_BUTTON (widget) || GTK_IS_ENTRY (widget)
		|| GTK_IS_TOOLBAR (widget) || GTK_IS_NOTEBOOK (widget)
		|| GTK_IS_MENU_SHELL (widget) || GTK_IS_MENU_ITEM (widget)
		|| GTK_IS_FRAME (widget) || GTK_IS_SCROLLED_WINDOW (widget)
		|| GTK_IS_PANED (widget) || GTK_IS_SCROLLBAR (widget)
		|| GTK_IS_PROGRESS_BAR (widget) || GTK_IS_SEPARATOR (widget);
}

/* Text views keep their own renderer and palette. Only their enclosing frame
 * changes when keyboard focus moves into or out of the text area. */
static GtkWidget *_e2_modern_ui_text_frame (GtkWidget *text)
{
	GtkWidget *frame = gtk_widget_get_parent (text);
	if (frame != NULL && GTK_IS_VIEWPORT (frame))
		frame = gtk_widget_get_parent (frame);
	return (frame != NULL && GTK_IS_SCROLLED_WINDOW (frame)) ? frame : NULL;
}

static gboolean _e2_modern_ui_text_focus (GtkWidget *widget,
	GdkEventFocus *event, gpointer unused)
{
	GtkWidget *frame = _e2_modern_ui_text_frame (widget);
	if (frame != NULL)
	{
#ifdef USE_GTK3_0
		GtkStyleContext *context = gtk_widget_get_style_context (frame);
		if (event->in)
			gtk_style_context_add_class (context, "e2-modern-input-focus");
		else
			gtk_style_context_remove_class (context, "e2-modern-input-focus");
#endif
		gtk_widget_queue_draw (frame);
	}
	return FALSE;
}

#ifdef USE_GTK3_0
static GtkCssProvider *provider;

static void _e2_modern_ui_button_state (GtkWidget *widget,
	GtkStateFlags previous, gpointer unused)
{
	GtkStateFlags state = gtk_widget_get_state_flags (widget);
	gboolean highlighted = gtk_widget_is_sensitive (widget)
		&& ((state & (GTK_STATE_FLAG_PRELIGHT | GTK_STATE_FLAG_ACTIVE | GTK_STATE_FLAG_FOCUSED))
			|| (GTK_IS_TOGGLE_BUTTON (widget)
				&& gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget))));
	GtkStyleContext *context = gtk_widget_get_style_context (widget);
	if (highlighted)
		gtk_style_context_add_class (context, "e2-modern-button-highlight");
	else
		gtk_style_context_remove_class (context, "e2-modern-button-highlight");
}

static void _e2_modern_ui_style (GtkWidget *widget)
{
	if (!_e2_modern_ui_chrome (widget)) return;
	GtkStyleContext *context = gtk_widget_get_style_context (widget);
	if (GTK_IS_BUTTON (widget)
		&& !gtk_style_context_has_class (context, "e2-modern-button"))
	{
		gtk_style_context_add_class (context, "e2-modern-button");
		g_signal_connect (widget, "state-flags-changed",
			G_CALLBACK (_e2_modern_ui_button_state), NULL);
		_e2_modern_ui_button_state (widget, 0, NULL);
	}
	gtk_style_context_add_provider (context, GTK_STYLE_PROVIDER (provider),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}
#else
/* GTK 2 has no CSS. Copy the resolved style (including all five state palettes,
 * font, RC properties and thicknesses), replacing only chrome painting. Keeping
 * the original thicknesses is essential: flattening must not resize controls. */
typedef GtkStyle E2FlatStyle;
typedef GtkStyleClass E2FlatStyleClass;
G_DEFINE_TYPE (E2FlatStyle, e2_flat_style, GTK_TYPE_STYLE)

static cairo_t *_e2_modern_ui_context (GdkWindow *window, GdkRectangle *area,
	gint *width, gint *height)
{
	if (*width == -1 || *height == -1)
	{
		gint w, h;
		gdk_drawable_get_size (window, &w, &h);
		if (*width == -1) *width = w;
		if (*height == -1) *height = h;
	}
	cairo_t *cr = gdk_cairo_create (window);
	if (area != NULL)
	{
		cairo_rectangle (cr, area->x, area->y, area->width, area->height);
		cairo_clip (cr);
	}
	return cr;
}

static void _e2_modern_ui_border (cairo_t *cr, const GdkColor *color,
	gint x, gint y, gint width, gint height)
{
	if (width <= 0 || height <= 0) return;
	gdk_cairo_set_source_color (cr, color);
	cairo_set_line_width (cr, 1.0);
	cairo_rectangle (cr, x + .5, y + .5, width - 1, height - 1);
	cairo_stroke (cr);
}

static gboolean _e2_modern_ui_button_highlight (GtkWidget *widget, GtkStateType state)
{
	return widget != NULL && GTK_IS_BUTTON (widget)
		&& gtk_widget_is_sensitive (widget) && state != GTK_STATE_INSENSITIVE
		&& (state == GTK_STATE_PRELIGHT || state == GTK_STATE_ACTIVE
			|| gtk_widget_has_focus (widget)
			|| (GTK_IS_TOGGLE_BUTTON (widget)
				&& gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget))));
}

static void _e2_modern_ui_shadow (GtkStyle *style, GdkWindow *window,
	GtkStateType state, GtkShadowType shadow, GdkRectangle *area,
	GtkWidget *widget, const gchar *detail, gint x, gint y, gint width, gint height)
{
	if (shadow == GTK_SHADOW_NONE) return;
	cairo_t *cr = _e2_modern_ui_context (window, area, &width, &height);
	gboolean input_focus = widget != NULL && GTK_IS_ENTRY (widget)
		&& gtk_widget_has_focus (widget);
	if (widget != NULL && GTK_IS_SCROLLED_WINDOW (widget))
	{
		GtkWidget *child = gtk_bin_get_child (GTK_BIN (widget));
		if (child != NULL && GTK_IS_VIEWPORT (child))
			child = gtk_bin_get_child (GTK_BIN (child));
		input_focus = child != NULL && GTK_IS_TEXT_VIEW (child)
			&& gtk_widget_has_focus (child);
	}
	_e2_modern_ui_border (cr, (input_focus || _e2_modern_ui_button_highlight (widget, state))
		? &style->bg[GTK_STATE_SELECTED]
		: &style->dark[state], x, y, width, height);
	cairo_destroy (cr);
}

static void _e2_modern_ui_flat_box (GtkStyle *style, GdkWindow *window,
	GtkStateType state, GtkShadowType shadow, GdkRectangle *area,
	GtkWidget *widget, const gchar *detail, gint x, gint y, gint width, gint height)
{
	/* GtkEntry::state-hint paints a focused entry_bg as ACTIVE. Pixmap themes
	 * use their normal entry artwork here; GtkStyle instead fills base[ACTIVE],
	 * which is often the selection color. Keep that hint on the border only.
	 * Actual text selections use their original selection palette. */
	if (widget != NULL && GTK_IS_ENTRY (widget) && state == GTK_STATE_ACTIVE
		&& detail != NULL && !strcmp (detail, "entry_bg"))
		state = GTK_STATE_NORMAL;
	GTK_STYLE_CLASS (e2_flat_style_parent_class)->draw_flat_box (style, window,
		state, shadow, area, widget, detail, x, y, width, height);
}

static void _e2_modern_ui_box (GtkStyle *style, GdkWindow *window,
	GtkStateType state, GtkShadowType shadow, GdkRectangle *area,
	GtkWidget *widget, const gchar *detail, gint x, gint y, gint width, gint height)
{
	cairo_t *cr = _e2_modern_ui_context (window, area, &width, &height);
	const GdkColor *color = &style->bg[state];
	if (detail != NULL && !strcmp (detail, "entry_bg"))
		color = &style->base[state];
	else if (detail != NULL && !strcmp (detail, "bar"))
		color = &style->bg[GTK_STATE_SELECTED];
	else if (state == GTK_STATE_PRELIGHT && detail != NULL
		&& !strcmp (detail, "menuitem"))
		/* GTK uses PRELIGHT for mouse and keyboard menu selection. Themes
		 * with painted menu artwork may leave its background equal to NORMAL;
		 * use their selection color when replacing that artwork with a fill. */
		color = &style->bg[GTK_STATE_SELECTED];
	gdk_cairo_set_source_color (cr, color);
	cairo_rectangle (cr, x, y, width, height);
	cairo_fill (cr);
	/* Keep the theme's button fill; hover, focus and toggled/pressed states
	 * use its selection color only on the existing border. */
	gboolean button_highlight = _e2_modern_ui_button_highlight (widget, state);
	if (shadow != GTK_SHADOW_NONE || button_highlight)
		_e2_modern_ui_border (cr, button_highlight ? &style->bg[GTK_STATE_SELECTED]
			: &style->dark[state], x, y, width, height);
	cairo_destroy (cr);
}

static void _e2_modern_ui_focus (GtkStyle *style, GdkWindow *window,
	GtkStateType state, GdkRectangle *area, GtkWidget *widget,
	const gchar *detail, gint x, gint y, gint width, gint height)
{
	cairo_t *cr = _e2_modern_ui_context (window, area, &width, &height);
	gboolean button_highlight = _e2_modern_ui_button_highlight (widget, state);
	_e2_modern_ui_border (cr, button_highlight ? &style->bg[GTK_STATE_SELECTED]
		: &style->fg[state], x, y, width, height);
	if (!button_highlight && width > 4 && height > 4)
		_e2_modern_ui_border (cr, &style->fg[state], x + 1, y + 1, width - 2, height - 2);
	cairo_destroy (cr);
}

static void _e2_modern_ui_hline (GtkStyle *style, GdkWindow *window,
	GtkStateType state, GdkRectangle *area, GtkWidget *widget,
	const gchar *detail, gint x1, gint x2, gint y)
{
	gint w = x2 - x1 + 1, h = 1;
	cairo_t *cr = _e2_modern_ui_context (window, area, &w, &h);
	gdk_cairo_set_source_color (cr, &style->dark[state]);
	cairo_rectangle (cr, x1, y, w, 1);
	cairo_fill (cr);
	cairo_destroy (cr);
}

static void _e2_modern_ui_vline (GtkStyle *style, GdkWindow *window,
	GtkStateType state, GdkRectangle *area, GtkWidget *widget,
	const gchar *detail, gint y1, gint y2, gint x)
{
	gint w = 1, h = y2 - y1 + 1;
	cairo_t *cr = _e2_modern_ui_context (window, area, &w, &h);
	gdk_cairo_set_source_color (cr, &style->dark[state]);
	cairo_rectangle (cr, x, y1, 1, h);
	cairo_fill (cr);
	cairo_destroy (cr);
}

static void _e2_modern_ui_gap (GtkStyle *style, GdkWindow *window,
	GtkStateType state, GtkShadowType shadow, GdkRectangle *area,
	GtkWidget *widget, const gchar *detail, gint x, gint y, gint width, gint height,
	GtkPositionType side, gint gap_x, gint gap_width)
{
	_e2_modern_ui_box (style, window, state, shadow, area, widget, detail,
		x, y, width, height);
	cairo_t *cr = _e2_modern_ui_context (window, area, &width, &height);
	gdk_cairo_set_source_color (cr, &style->bg[state]);
	switch (side)
	{
		case GTK_POS_TOP: cairo_rectangle (cr, x + gap_x, y, gap_width, 1); break;
		case GTK_POS_BOTTOM: cairo_rectangle (cr, x + gap_x, y + height - 1, gap_width, 1); break;
		case GTK_POS_LEFT: cairo_rectangle (cr, x, y + gap_x, 1, gap_width); break;
		case GTK_POS_RIGHT: cairo_rectangle (cr, x + width - 1, y + gap_x, 1, gap_width); break;
	}
	cairo_fill (cr);
	cairo_destroy (cr);
}

static void _e2_modern_ui_extension (GtkStyle *style, GdkWindow *window,
	GtkStateType state, GtkShadowType shadow, GdkRectangle *area,
	GtkWidget *widget, const gchar *detail, gint x, gint y, gint width, gint height,
	GtkPositionType side)
{
	_e2_modern_ui_gap (style, window, state, shadow, area, widget, detail,
		x, y, width, height, side, 1,
		(side == GTK_POS_TOP || side == GTK_POS_BOTTOM) ? width - 2 : height - 2);
}

static void _e2_modern_ui_slider (GtkStyle *style, GdkWindow *window,
	GtkStateType state, GtkShadowType shadow, GdkRectangle *area,
	GtkWidget *widget, const gchar *detail, gint x, gint y, gint width, gint height,
	GtkOrientation orientation)
{
	_e2_modern_ui_box (style, window, state, shadow, area, widget, detail,
		x, y, width, height);
}

static void e2_flat_style_class_init (E2FlatStyleClass *klass)
{
	klass->draw_box = _e2_modern_ui_box;
	klass->draw_flat_box = _e2_modern_ui_flat_box;
	klass->draw_shadow = _e2_modern_ui_shadow;
	klass->draw_focus = _e2_modern_ui_focus;
	klass->draw_hline = _e2_modern_ui_hline;
	klass->draw_vline = _e2_modern_ui_vline;
	klass->draw_box_gap = _e2_modern_ui_gap;
	klass->draw_extension = _e2_modern_ui_extension;
	klass->draw_slider = _e2_modern_ui_slider;
}

static void e2_flat_style_init (E2FlatStyle *style) {}

/* Use GTK's modifier-style/engine mechanism rather than set_style(): an
 * explicit GtkStyle would suppress later gtk_widget_modify_bg/font calls. */
typedef GtkRcStyle E2FlatRcStyle;
typedef GtkRcStyleClass E2FlatRcStyleClass;
G_DEFINE_TYPE (E2FlatRcStyle, e2_flat_rc_style, GTK_TYPE_RC_STYLE)

static GtkStyle *_e2_modern_ui_create_style (GtkRcStyle *rc)
{
	return g_object_new (e2_flat_style_get_type (), NULL);
}

static void e2_flat_rc_style_class_init (E2FlatRcStyleClass *klass)
{
	klass->create_style = _e2_modern_ui_create_style;
}

static void e2_flat_rc_style_init (E2FlatRcStyle *rc) {}

static void _e2_modern_ui_style (GtkWidget *widget)
{
	static gboolean setting_style;
	/* Before mapping, parent-dependent RC rules have not necessarily resolved.
	 * Capturing them earlier would freeze GTK's fallback palette/metrics. */
	if (setting_style || !GTK_WIDGET_MAPPED (widget)
		|| !_e2_modern_ui_chrome (widget)) return;
	GtkStyle *original = gtk_widget_get_style (widget);
	if (G_TYPE_CHECK_INSTANCE_TYPE (original, e2_flat_style_get_type ())) return;
	setting_style = TRUE;
	GtkRcStyle *rc = g_object_new (e2_flat_rc_style_get_type (), NULL);
	GTK_RC_STYLE_CLASS (e2_flat_rc_style_parent_class)->merge (rc,
		gtk_widget_get_modifier_style (widget));
	if (original->rc_style != NULL)
		GTK_RC_STYLE_CLASS (e2_flat_rc_style_parent_class)->merge (rc, original->rc_style);
	for (guint i = 0; i < 5; i++)
	{
		rc->fg[i] = original->fg[i];
		rc->bg[i] = original->bg[i];
		rc->text[i] = original->text[i];
		rc->base[i] = original->base[i];
		rc->color_flags[i] = GTK_RC_FG | GTK_RC_BG | GTK_RC_TEXT | GTK_RC_BASE;
	}
	rc->xthickness = original->xthickness;
	rc->ythickness = original->ythickness;
	rc->engine_specified = TRUE;
	gtk_widget_modify_style (widget, rc);
	g_object_unref (rc);
	setting_style = FALSE;
}
#endif

static gboolean _e2_modern_ui_apply (gpointer data)
{
	CLOSEBGL
	GtkWidget *widget = data;
	g_object_set_data (G_OBJECT (widget), "e2-modern-pending", NULL);
	if (gtk_widget_get_mapped (widget)) _e2_modern_ui_style (widget);
	OPENBGL
	return FALSE;
}

static void _e2_modern_ui_release (gpointer data)
{
	CLOSEBGL
	g_object_unref (data);
	OPENBGL
}

static gboolean _e2_modern_ui_mapped (GSignalInvocationHint *hint,
	guint n_values, const GValue *values, gpointer data)
{
	GtkWidget *widget = g_value_get_object (&values[0]);
	if (GTK_IS_TEXT_VIEW (widget)
		&& g_object_get_data (G_OBJECT (widget), "e2-modern-text-focus") == NULL)
	{
		g_object_set_data (G_OBJECT (widget), "e2-modern-text-focus", GINT_TO_POINTER (1));
		g_signal_connect (widget, "focus-in-event", G_CALLBACK (_e2_modern_ui_text_focus), NULL);
		g_signal_connect (widget, "focus-out-event", G_CALLBACK (_e2_modern_ui_text_focus), NULL);
	}
	/* A combo can assign its internal button's final style after the child has
	 * mapped. Wait for the complete map/style emission and native allocation. */
	if (_e2_modern_ui_chrome (widget)
		&& g_object_get_data (G_OBJECT (widget), "e2-modern-pending") == NULL)
	{
		g_object_set_data (G_OBJECT (widget), "e2-modern-pending", GINT_TO_POINTER (1));
		g_idle_add_full (G_PRIORITY_DEFAULT_IDLE, _e2_modern_ui_apply,
			g_object_ref (widget), _e2_modern_ui_release);
	}
	return TRUE;
}

void e2_modern_ui_init (void)
{
	if (initialized) return;
	initialized = TRUE;
	enabled = e2_option_bool_get ("modern-ui");
	if (!enabled) return;
#ifdef USE_GTK3_0
	/* Keep the theme's metrics and palettes. Input focus and button highlights
	 * use its selection color on borders only; file/text renderers and VTE
	 * retain their styles. */
	provider = gtk_css_provider_new ();
	GError *error = NULL;
	gtk_css_provider_load_from_data (provider,
		"* { background-image: none; box-shadow: none; text-shadow: none;"
		" border-image: none; border-radius: 3px; }"
		" .e2-modern-input-focus { border-color: @theme_selected_bg_color; }"
		" .e2-modern-button-highlight {"
		" border-color: @theme_selected_bg_color; outline-color: @theme_selected_bg_color; }",
		-1, &error);
	if (error != NULL)
	{
		g_warning ("Cannot load modern appearance: %s", error->message);
		g_error_free (error);
		g_object_unref (provider);
		provider = NULL;
		enabled = FALSE;
		return;
	}
#endif
	/* gtk_init() need not have instantiated a widget yet. Its class registers
	 * the signals we hook; force that before looking up their IDs. */
	gpointer widget_class = g_type_class_ref (GTK_TYPE_WIDGET);
	g_signal_add_emission_hook (g_signal_lookup ("map", GTK_TYPE_WIDGET), 0,
		_e2_modern_ui_mapped, NULL, NULL);
#ifndef USE_GTK3_0
	g_signal_add_emission_hook (g_signal_lookup ("style-set", GTK_TYPE_WIDGET), 0,
		_e2_modern_ui_mapped, NULL, NULL);
#endif
	g_type_class_unref (widget_class);
}

/* Embedded vector artwork uses the existing icon slots and pixel sizes. No
 * extra files, SVG-library link dependency, or altered toolbar config is needed.
 * If an SVG loader is unavailable the caller keeps the original icon. */
#include "e2_modern_icons.h"

GdkPixbuf *e2_modern_ui_icon (const gchar *name, gint width, gint height)
{
	if (!enabled || name == NULL || width <= 0 || height <= 0
		|| width > 512 || height > 512
		|| strchr (name, G_DIR_SEPARATOR) != NULL
		|| e2_option_bool_get ("use-icon-dir"))
		return NULL;
	gchar *base = g_strdup (name);
	gchar *suffix = strrchr (base, '.');
	if (suffix != NULL && (!strcmp (suffix, ".png") || !strcmp (suffix, ".svg")))
		*suffix = '\0';
	suffix = strrchr (base, '_');
	if (suffix != NULL && g_ascii_isdigit (suffix[1])) *suffix = '\0';
	const gchar *paths = NULL;
	guint i;
	for (i = 0; i < G_N_ELEMENTS (modern_icons); i++)
		if (!strcmp (base, modern_icons[i].name))
		{
			paths = modern_icons[i].paths;
			break;
		}
	if (paths == NULL) { g_free (base); return NULL; }

	GtkWidget *probe = NULL;
	GtkWidget *widget = app.main_window;
	if (widget == NULL)
	{
		probe = gtk_window_new (GTK_WINDOW_TOPLEVEL);
		widget = probe;
	}
	guint red, green, blue;
#ifdef USE_GTK3_0
	GdkRGBA color;
	gtk_style_context_get_color (gtk_widget_get_style_context (widget),
		GTK_STATE_FLAG_NORMAL, &color);
	red = CLAMP (color.red * 255 + .5, 0, 255);
	green = CLAMP (color.green * 255 + .5, 0, 255);
	blue = CLAMP (color.blue * 255 + .5, 0, 255);
#else
	gtk_widget_ensure_style (widget);
	GdkColor color = gtk_widget_get_style (widget)->fg[GTK_STATE_NORMAL];
	red = color.red / 257; green = color.green / 257; blue = color.blue / 257;
#endif
	if (probe != NULL) gtk_widget_destroy (probe);
	gchar *key = g_strdup_printf ("%s/%dx%d/%02x%02x%02x", base, width, height, red, green, blue);
	g_free (base);
	if (icons == NULL)
		icons = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	GdkPixbuf *pixbuf = g_hash_table_lookup (icons, key);
	if (pixbuf != NULL) { g_free (key); return pixbuf; }
	gchar *svg = g_strdup_printf (
		"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\""
		" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"#%02x%02x%02x\""
		" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\">%s</svg>",
		width, height, red, green, blue, paths);
	GdkPixbufLoader *loader = gdk_pixbuf_loader_new_with_type ("svg", NULL);
	if (loader != NULL)
	{
		gboolean written = gdk_pixbuf_loader_write (loader, (guchar *)svg, strlen (svg), NULL);
		gboolean closed = gdk_pixbuf_loader_close (loader, NULL);
		if (written && closed)
		{
			pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
			if (pixbuf != NULL) g_object_ref (pixbuf);
		}
		g_object_unref (loader);
	}
	g_free (svg);
	if (pixbuf != NULL) g_hash_table_insert (icons, key, pixbuf);
	else g_free (key);
	return pixbuf;
}

void e2_modern_ui_clear_icons (void)
{
	if (icons != NULL)
	{
		g_hash_table_destroy (icons);
		icons = NULL;
	}
}
#endif
