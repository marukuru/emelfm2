/* Single-instance activation. Licensed under GPL version 3 or later. */
#ifndef __E2_SINGLE_INSTANCE_H__
#define __E2_SINGLE_INSTANCE_H__

#include "emelfm2.h"

gboolean e2_single_instance_start (void);
void e2_single_instance_sync (void);
void e2_single_instance_cleanup (void);

#endif
