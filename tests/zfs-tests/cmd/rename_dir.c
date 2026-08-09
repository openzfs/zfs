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

int
main(void)
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
			if (c_count)
				(void) fprintf(stderr, "c_count: %d", c_count);
		}
		_exit(0);

	default:
		while (i > 0) {
			int p_count = 0;
			if (rename("1", "a/b/c/d/e/1") == 0)
				p_count++;
			if (rename("a/b/c/d/e/1", "1") == 0)
				p_count++;
			if (p_count)
				(void) fprintf(stderr, "p_count: %d", p_count);
		}
		return (0);
	}
}
