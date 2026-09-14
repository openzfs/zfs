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
 *
 * Copyright (C) 2008 MacZFS
 * Copyright (C) 2013, 2020 Jorgen Lundman <lundman@lundman.net>
 *
 */


#ifndef _SPL_TSD_H
#define	_SPL_TSD_H

#include <sys/types.h>
#include <sys/thread.h>

#define	TSD_HASH_TABLE_BITS_DEFAULT	9
#define	TSD_KEYS_MAX			32768
#define	DTOR_PID			(PID_MAX_LIMIT+1)
#define	PID_KEY				(TSD_KEYS_MAX+1)

typedef void (*dtor_func_t)(void *);

extern int tsd_set(uint_t, void *);
extern void *tsd_get(uint_t);
extern void *tsd_get_by_thread(uint_t, kthread_t *);
extern void tsd_create(uint_t *, dtor_func_t);
extern void tsd_destroy(uint_t *);
extern void tsd_exit(void);

uint64_t spl_tsd_size(void);
void tsd_thread_exit(void);
int spl_tsd_init(void);
void spl_tsd_fini(void);

#endif /* _SPL_TSD_H */
