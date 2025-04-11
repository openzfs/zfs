// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright (c) 2025 by Lawrence Livermore National Security, LLC.
 */
/*
 * This file contains zfs_dbgmsg() specific functions that are not OS or
 * userspace specific.
 */
#if !defined(_KERNEL)
#include <string.h>
#endif

#include <sys/zfs_context.h>
#include <sys/zfs_debug.h>
#include <sys/nvpair.h>

/*
 * Given a multi-line string, print out one of the lines and return a pointer
 * to the next line. Lines are demarcated by '\n'.  Note: this modifies the
 * input string (buf[]).
 *
 * This function is meant to be used in a loop like:
 * 	while (buf != NULL)
 * 		buf = kernel_print_one_line(buf);
 *
 * This function is useful for printing large, multi-line text buffers.
 *
 * Returns the pointer to the beginning of the next line in buf[], or NULL
 * if it's the last line, or nothing more to print.
 */
static char *
zfs_dbgmsg_one_line(char *buf)
{
	char *nl;
	if (!buf)
		return (NULL);

	nl = strchr(buf, '\n');
	if (nl == NULL) {
		__zfs_dbgmsg(buf);
		return (NULL); /* done */
	}
	*nl = '\0';
	__zfs_dbgmsg(buf);

	return (nl + 1);
}

/*
 * Dump an nvlist tree to dbgmsg.
 *
 * This is the zfs_dbgmsg version of userspace's dump_nvlist() from libnvpair.
 */
void
__zfs_dbgmsg_nvlist(nvlist_t *nv)
{
	int len;
	char *buf;

	len = nvlist_snprintf(NULL, 0, nv, 4);
	len++; /* Add null terminator */

	buf = vmem_alloc(len, KM_SLEEP);
	if (buf == NULL)
		return;

	(void) nvlist_snprintf(buf, len, nv, 4);

	while (buf != NULL)
		buf = zfs_dbgmsg_one_line(buf);

	vmem_free(buf, len);
}

#ifdef _KERNEL
EXPORT_SYMBOL(__zfs_dbgmsg_nvlist);
#endif
