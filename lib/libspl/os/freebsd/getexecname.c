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

#include <stdint.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include "../../libspl_impl.h"

__attribute__((visibility("hidden"))) ssize_t
getexecname_impl(char *execname)
{
	size_t len = PATH_MAX;
	int name[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1};

	if (sysctl(name, nitems(name), execname, &len, NULL, 0) != 0)
		return (-1);

	return (len);
}
