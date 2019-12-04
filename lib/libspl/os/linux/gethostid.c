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
	if (env) {
		hostid = strtoull(env, NULL, 0);
		return (hostid & HOSTID_MASK);
	}

	f = fopen("/sys/module/spl/parameters/spl_hostid", "r");
	if (!f)
		return (0);

	if (fscanf(f, "%lu", &hostid) != 1)
		hostid = 0;

	fclose(f);

	return (hostid & HOSTID_MASK);
}

unsigned long
get_system_hostid(void)
{
	unsigned long system_hostid = get_spl_hostid();
	/*
	 * We do not use the library call gethostid() because
	 * it generates a hostid value that the kernel is
	 * unaware of, if the spl_hostid module parameter has not
	 * been set and there is no system hostid file (e.g.
	 * /etc/hostid).  The kernel and userspace must agree.
	 * See comments above hostid_read() in the SPL.
	 */
	if (system_hostid == 0) {
		int fd, rc;
		unsigned long hostid;
		int hostid_size = 4;  /* 4 bytes regardless of arch */

		fd = open("/etc/hostid", O_RDONLY);
		if (fd >= 0) {
			rc = read(fd, &hostid, hostid_size);
			if (rc > 0)
				system_hostid = (hostid & HOSTID_MASK);
			close(fd);
		}
	}
	return (system_hostid);
}
