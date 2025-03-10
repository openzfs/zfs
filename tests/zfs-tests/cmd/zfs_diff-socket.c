// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2017, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
 */

#include <fcntl.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char *argv[])
{
	struct sockaddr_un sock;
	int fd;
	char *path;
	size_t size;
	if (argc != 2) {
		fprintf(stderr, "usage: %s /path/to/socket\n", argv[0]);
		exit(1);
	}
	path = argv[1];
	size =  sizeof (sock.sun_path);
	(void) snprintf(sock.sun_path, size, "%s", path);

	sock.sun_family = AF_UNIX;
	if ((fd = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
		perror("socket");
		return (1);
	}
	if (bind(fd, (struct sockaddr *)&sock, sizeof (struct sockaddr_un))) {
		perror("bind");
		return (1);
	}
	if (close(fd)) {
		perror("close");
		return (1);
	}
	return (0);
}
