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
 * Copyright (c) 2016 Stian Ellingsen.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int
usage(void)
{
	fprintf(stderr, "Usage: umount.zfs [-flnrv] {directory|device}\n");
	return (1);
}

int
main(int argc, char **argv)
{
	char *cmd[] = { "/bin/umount", "-t", "zfs", "-i" };
	char *argv2[argc + 4];
	char c;
	while ((c = getopt(argc, argv, "flnrv")) != -1) {
		if (c == '?')
			return (usage());
	}
	if (argc - optind != 1)
		return (usage());
	memcpy(argv2, cmd, sizeof (cmd));
	memcpy(argv2 + 4, argv + 1, argc * sizeof (char *));
	execv(*argv2, argv2);
	return (127);
}
