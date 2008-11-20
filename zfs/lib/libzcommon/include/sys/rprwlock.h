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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_RPRWLOCK_H
#define	_SYS_RPRWLOCK_H



#include <sys/inttypes.h>
#include <sys/list.h>
#include <sys/zfs_context.h>
#include <sys/refcount.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct rprwlock {
	kmutex_t	rw_lock;
	kthread_t	*rw_writer;
	kcondvar_t	rw_cv;
	refcount_t	rw_count;
} rprwlock_t;

void rprw_init(rprwlock_t *rwl);
void rprw_destroy(rprwlock_t *rwl);
void rprw_enter_read(rprwlock_t *rwl, void *tag);
void rprw_enter_write(rprwlock_t *rwl, void *tag);
void rprw_enter(rprwlock_t *rwl, krw_t rw, void *tag);
void rprw_exit(rprwlock_t *rwl, void *tag);
boolean_t rprw_held(rprwlock_t *rwl, krw_t rw);
#define	RPRW_READ_HELD(x)	rprw_held(x, RW_READER)
#define	RPRW_WRITE_HELD(x)	rprw_held(x, RW_WRITER)

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_RPRWLOCK_H */
