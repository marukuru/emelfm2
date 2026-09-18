/* GTK-independent StatusNotifierItem transport. GPL version 3 or later. */
#ifndef __E2_TRAY_INDICATOR_H__
#define __E2_TRAY_INDICATOR_H__
#include "emelfm2.h"
#if GLIB_CHECK_VERSION(2,26,0) && defined(USE_GTK2_10)
gboolean e2_tray_indicator_available (void);
GObject *e2_tray_indicator_new (const gchar *id, const gchar *icon, gint category);
void e2_tray_indicator_set_menu (GObject *object, GtkMenu *menu);
void e2_tray_indicator_set_status (GObject *object, gint status);
void e2_tray_indicator_set_icon (GObject *object, const gchar *icon);
void e2_tray_indicator_set_attention_icon (GObject *object, const gchar *icon);
void e2_tray_indicator_set_label (GObject *object, const gchar *label, const gchar *guide);
void e2_tray_indicator_stop (GObject *object);
#endif
#endif
