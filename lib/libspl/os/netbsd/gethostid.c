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
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/systeminfo.h>

unsigned long
get_system_hostid(void)
{
	char *env;
	int fd;
	uint32_t file_hostid;

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

	/*
	 * zgenhostid(8) writes /etc/hostid.  Prefer it over gethostid(3)
	 * so a just-written value is visible even if sethostid(3) is
	 * unavailable to this process.
	 */
	fd = open("/etc/hostid", O_RDONLY | O_CLOEXEC);
	if (fd >= 0) {
		if (read(fd, &file_hostid, sizeof (file_hostid)) ==
		    sizeof (file_hostid) && file_hostid != 0) {
			(void) close(fd);
			return (file_hostid & HOSTID_MASK);
		}
		(void) close(fd);
	}

	return (gethostid());
}
