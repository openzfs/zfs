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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

/*
 * Assertion:
 * Create two directory trees in zfs filesystem, and rename
 * directory across the directory structure. ZFS can handle
 * the race situation.
 */

/*
 * Need to create the following directory structures before
 * running this program:
 *
 * mkdir -p 1/2/3/4/5 a/b/c/d/e
 */


#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <strings.h>

int
main(int argc, char *argvp[])
{
	int i = 1;

	switch (fork()) {
	case -1:
		perror("fork");
		exit(1);
		break;
	case 0:
		while (i > 0) {
			int c_count = 0;
			if (rename("a/b/c", "1/2/3/c") == 0)
				c_count++;
			if (rename("1/2/3/c", "a/b/c") == 0)
				c_count++;
			if (c_count) {
				(void) fprintf(stderr, "c_count: %d", c_count);
			}
		}
		break;
	default:
		while (i > 0) {
			int p_count = 0;
			if (rename("1", "a/b/c/d/e/1") == 0)
				p_count++;
			if (rename("a/b/c/d/e/1", "1") == 0)
				p_count++;
			if (p_count) {
				(void) fprintf(stderr, "p_count: %d", p_count);
			}
		}
		break;
	}

	return (0);
}
