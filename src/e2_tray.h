/* Tray integration. Licensed under GPL version 3 or later. */
#ifndef __E2_TRAY_H__
#define __E2_TRAY_H__

#include "emelfm2.h"

void e2_tray_sync (void);
GtkWidget *e2_tray_menu_item_new (const gchar *label, const gchar *icon, gboolean mnemonic);
void e2_tray_cleanup (void);
gboolean e2_tray_is_active (void);
gboolean e2_tray_is_hidden (void);
void e2_tray_show_main (void);
void e2_tray_show_for_review (void);
void e2_tray_set_attention (guint count);
void e2_tray_register_window (GtkWidget *window);
void e2_tray_register_transfer (GtkWidget *window);
void e2_tray_prepare_quit_dialog (GtkWidget *window);
gboolean e2_tray_defer_dialog (GtkWidget *window, gboolean question, gboolean show_all);
void e2_tray_wait_dialog (GtkWidget *window);
void e2_tray_windows_hide (void);
void e2_tray_windows_restore (void);
void e2_tray_windows_sync (gboolean enabled);
void e2_tray_windows_menu (GtkWidget *menu);
void e2_tray_windows_cleanup (void);
void e2_tray_review_pending (void);
void e2_tray_notify_pending (guint count);

#endif
