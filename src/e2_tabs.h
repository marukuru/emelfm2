/* File-pane tabs. Licensed under GPL version 3 or later. */
#ifndef __E2_TABS_H__
#define __E2_TABS_H__

#include "emelfm2.h"

void e2_tabs_pack (GtkWidget *panes);
void e2_tabs_update_title (void);
void e2_tabs_rebuild_begin (void);
void e2_tabs_rebuild_end (void);
void e2_tabs_cleanup (void);
gboolean e2_tabs_key (GtkWidget *widget, GdkEventKey *event, gpointer data);

#endif
