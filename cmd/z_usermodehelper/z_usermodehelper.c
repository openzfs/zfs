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

int
mount(int argc, char **argv) {
	char *argv2[] = { "/sbin/mount.zfs", "--", NULL, NULL, NULL };
	if (argc != 3)
		return (1);
	argv2[2] = argv[1];
	argv2[3] = argv[2];
	execv(*argv2, argv2);
	return (127);
}

int
umount(int argc, char **argv) {
	char *argv2[] = { "/bin/umount", "-t", "zfs", "-f", NULL, NULL, NULL };
	int i = 3;
	char c;
	while ((c = getopt(argc, argv, "f")) != -1) {
		switch (c) {
		case 'f':
			i = 4;
			break;
		default:
			return (1);
		}
	}
	if (argc - optind != 1)
		return (1);
	argv2[i] = "--";
	argv2[i + 1] = argv[optind];
	execv(*argv2, argv2);
	return (127);
}

	int
main(int argc, char **argv)
{
	int (*cmd)(int, char **);

	if (argc < 2)
		return (1);

	if (strcmp(argv[1], "mount") == 0)
		cmd = mount;
	else if (strcmp(argv[1], "umount") == 0)
		cmd = umount;
	else
		return (1);

	freopen("/dev/null", "r", stdin);
	freopen("/dev/null", "w", stdout);
	freopen("/dev/null", "w", stderr);

	return (cmd(argc - 1, argv + 1));
}
