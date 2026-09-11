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
#define	pthread_setname_np(tid, name) (void) name

#endif
