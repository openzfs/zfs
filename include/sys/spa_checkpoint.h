// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * Copyright (c) 2017 by Delphix. All rights reserved.
 */

#ifndef _SYS_SPA_CHECKPOINT_H
#define	_SYS_SPA_CHECKPOINT_H

#include <sys/zthr.h>

typedef struct spa_checkpoint_info {
	uint64_t sci_timestamp; /* when checkpointed uberblock was synced  */
	uint64_t sci_dspace;    /* disk space used by checkpoint in bytes */
} spa_checkpoint_info_t;

int spa_checkpoint(const char *);
int spa_checkpoint_discard(const char *);

boolean_t spa_checkpoint_discard_thread_check(void *, zthr_t *);
void spa_checkpoint_discard_thread(void *, zthr_t *);

int spa_checkpoint_get_stats(spa_t *, pool_checkpoint_stat_t *);

#endif /* _SYS_SPA_CHECKPOINT_H */
