/* Progress window for copy and move tasks.
 * Released under the GNU General Public License, version 3 or later. */
#ifndef __E2_TASK_PROGRESS_H__
#define __E2_TASK_PROGRESS_H__

#include "emelfm2.h"
#include "e2_task.h"

typedef struct _E2_TaskProgress E2_TaskProgress;

E2_TaskProgress *e2_task_progress_new (E2_ActionTaskData *qed,
	const gchar *destdir, gboolean move);
gboolean e2_task_progress_item (E2_TaskProgress *pg, guint index);
void e2_task_progress_answered (E2_TaskProgress *pg);
gboolean e2_task_progress_stopped (void);
void e2_task_progress_free (E2_TaskProgress *pg);

#endif //ndef __E2_TASK_PROGRESS_H__
