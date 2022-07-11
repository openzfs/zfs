/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * This header file distributed under the terms of the CDDL.
 * Portions Copyright 2008 Sun Microsystems, Inc. All Rights reserved.
 */
#ifndef _SYS_STACK_H
#define	_SYS_STACK_H

#include <pthread.h>

#define	STACK_BIAS	0

#ifdef __USE_GNU

static inline int
stack_getbounds(stack_t *sp)
{
	pthread_attr_t attr;
	int rc;

	rc = pthread_getattr_np(pthread_self(), &attr);
	if (rc)
		return (rc);

	rc = pthread_attr_getstack(&attr, &sp->ss_sp, &sp->ss_size);
	if (rc == 0)
		sp->ss_flags = 0;

	pthread_attr_destroy(&attr);

	return (rc);
}

static inline int
thr_stksegment(stack_t *sp)
{
	int rc;

	rc = stack_getbounds(sp);
	if (rc)
		return (rc);

	/*
	 * thr_stksegment() is expected to set sp.ss_sp to the high stack
	 * address, but the stack_getbounds() interface is expected to
	 * set sp.ss_sp to the low address.  Adjust accordingly.
	 */
	sp->ss_sp = (void *)(((uintptr_t)sp->ss_sp) + sp->ss_size);
	sp->ss_flags = 0;

	return (rc);
}

#endif /* __USE_GNU */
#endif /* _SYS_STACK_H */
