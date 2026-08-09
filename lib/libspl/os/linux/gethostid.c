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

static unsigned long
get_spl_hostid(void)
{
	FILE *f;
	unsigned long hostid;
	char *env;

	/*
	 * Allow the hostid to be subverted for testing.
	 */
	env = getenv("ZFS_HOSTID");
	if (env)
		return (strtoull(env, NULL, 0));

	f = fopen("/proc/sys/kernel/spl/hostid", "re");
	if (!f)
		return (0);

	if (fscanf(f, "%lx", &hostid) != 1)
		hostid = 0;

	fclose(f);

	return (hostid);
}

unsigned long
get_system_hostid(void)
{
	unsigned long hostid = get_spl_hostid();
	uint32_t system_hostid;

	/*
	 * We do not use gethostid(3) because it can return a bogus ID,
	 * depending on the libc and /etc/hostid presence,
	 * and the kernel and userspace must agree.
	 * See comments above hostid_read() in the SPL.
	 */
	if (hostid == 0) {
		int fd = open("/etc/hostid", O_RDONLY | O_CLOEXEC);
		if (fd >= 0) {
			if (read(fd, &system_hostid, sizeof (system_hostid))
			    != sizeof (system_hostid))
				hostid = 0;
			else
				hostid = system_hostid;
			(void) close(fd);
		}
	}

	return (hostid & HOSTID_MASK);
}
