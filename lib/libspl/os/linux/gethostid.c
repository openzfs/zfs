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

	/*
	 * We do not use gethostid(3) because it can return a bogus ID,
	 * depending on the libc and /etc/hostid presence,
	 * and the kernel and userspace must agree.
	 * See comments above hostid_read() in the SPL.
	 */
	if (hostid == 0) {
		int fd = open("/etc/hostid", O_RDONLY | O_CLOEXEC);
		if (fd >= 0) {
			if (read(fd, &hostid, 4) < 0)
				hostid = 0;
			(void) close(fd);
		}
	}

	return (hostid & HOSTID_MASK);
}
