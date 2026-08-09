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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */


#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "libspl_impl.h"


const char *
getexecname(void)
{
	static char execname[PATH_MAX + 1] = "";
	static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;

	char *ptr = execname;
	ssize_t rc;

	(void) pthread_mutex_lock(&mtx);

	if (strlen(execname) == 0) {
		rc = getexecname_impl(execname);
		if (rc == -1) {
			execname[0] = '\0';
			ptr = NULL;
		} else {
			execname[rc] = '\0';
		}
	}

	(void) pthread_mutex_unlock(&mtx);
	return (ptr);
}
