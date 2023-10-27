/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
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

#define	TSD_HASH_TABLE_BITS_DEFAULT	9
#define	TSD_KEYS_MAX			32768
#define	DTOR_PID			(PID_MAX_LIMIT+1)
#define	PID_KEY				(TSD_KEYS_MAX+1)

typedef void (*dtor_func_t)(void *);

extern int tsd_set(uint_t, void *);
extern void *tsd_get(uint_t);
extern void *tsd_get_by_thread(uint_t, thread_t);
extern void tsd_create(uint_t *, dtor_func_t);
extern void tsd_destroy(uint_t *);
extern void tsd_exit(void);

uint64_t spl_tsd_size(void);
void tsd_thread_exit(void);
int spl_tsd_init(void);
void spl_tsd_fini(void);

#endif /* _SPL_TSD_H */
