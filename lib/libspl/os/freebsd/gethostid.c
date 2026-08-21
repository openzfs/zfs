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
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 */

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/systeminfo.h>

unsigned long
get_system_hostid(void)
{
	char *env;

	/*
	 * Allow the hostid to be subverted for testing.  A value which
	 * parses as zero is ignored, as it is on Linux, so that the
	 * system hostid is used instead.
	 */
	env = getenv("ZFS_HOSTID");
	if (env != NULL) {
		unsigned long hostid = strtoull(env, NULL, 0);
		if (hostid != 0)
			return (hostid & HOSTID_MASK);
	}

	return (gethostid());
}
