/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LIBSPL_SYS_PROCESSOR_H
#define	_LIBSPL_SYS_PROCESSOR_H

typedef int	processorid_t;

#ifdef __linux__
#include <sched.h>
static inline processorid_t
getcpuid(void) {
	processorid_t pid = sched_getcpu();
	if (pid == -1)
		return (0);
	return (pid);
}
#elif defined(__FreeBSD__)
#include <libutil.h>
#include <sys/types.h>
#include <sys/user.h>
#include <unistd.h>
static inline processorid_t
getcpuid(void) {
	struct kinfo_proc * kp = kinfo_getproc(getpid());
	if (kp == NULL)
		return (0);
	processorid_t pid = kp->ki_oncpu;
	free(kp);
	return (pid);
}
#else
#define	getcpuid() (0)
#endif

#endif
