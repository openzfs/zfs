/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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


#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <limits.h>

#include "zfs_config.h"

#ifndef HAVE_GETEXECNAME

const char *getexecname(void)
{
#ifdef __linux__
	static char execname[PATH_MAX + 1];
	/* Must be MT-safe */
	static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

	pthread_mutex_lock(&mtx);

	if (strlen(execname) == 0) {
		ssize_t rc = readlink("/proc/self/exe", execname, sizeof(execname - 1));
		if (rc == -1) {
			execname[0] = '\0';
			pthread_mutex_unlock(&mtx);
			return NULL;
		} else
			execname[rc] = '\0';
	}
	pthread_mutex_unlock(&mtx);

	return execname;
#else
	return NULL;
#endif
}

#endif
