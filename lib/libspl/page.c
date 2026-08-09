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

#include <unistd.h>
#include <sys/param.h>

static size_t pagesize = 0;

size_t
spl_pagesize(void)
{
	if (pagesize == 0)
		pagesize = sysconf(_SC_PAGESIZE);

	return (pagesize);
}
