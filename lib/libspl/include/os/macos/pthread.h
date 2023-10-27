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

#ifndef _LIBSPL_PTHREAD_H
#define	_LIBSPL_PTHREAD_H

#include_next <pthread.h>

/*
 * macOS does not have pthread_setname_np(tid, name) but rather their own
 * pthread_setname_np(name); which sets the name from inside the thread.
 * As we have been unable to find a macOS utility that actually displays
 * the thread-names (let us know if you find one) we will skip setting
 * names for (userland) threads.
 */
#define	pthread_setname_np(tid, name)

#endif
