/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <sys/dmu_ctl.h>
#include <sys/dmu_ctl_impl.h>

int dctl_read_msg(int fd, dctl_cmd_t *cmd)
{
	int error;

	/*
	 * First, read only the magic number and the protocol version.
	 *
	 * This prevents blocking forever in case the size of dctl_cmd_t
	 * shrinks in future protocol versions.
	 */
	error = dctl_read_data(fd, cmd, DCTL_CMD_HEADER_SIZE);

	if (!error &&cmd->dcmd_magic != DCTL_MAGIC) {
		fprintf(stderr, "%s(): invalid magic number\n", __func__);
		error = EIO;
	}

	if (!error && cmd->dcmd_version != DCTL_PROTOCOL_VER) {
		fprintf(stderr, "%s(): invalid protocol version\n", __func__);
		error = ENOTSUP;
	}

	if (error)
		return error;

	/* Get the rest of the command */
	return dctl_read_data(fd, (caddr_t) cmd + DCTL_CMD_HEADER_SIZE,
	    sizeof(dctl_cmd_t) - DCTL_CMD_HEADER_SIZE);
}

int dctl_send_msg(int fd, dctl_cmd_t *cmd)
{
	cmd->dcmd_magic = DCTL_MAGIC;
	cmd->dcmd_version = DCTL_PROTOCOL_VER;

	return dctl_send_data(fd, cmd, sizeof(dctl_cmd_t));
}

int dctl_read_data(int fd, void *ptr, size_t size)
{
	size_t read = 0;
	size_t left = size;
	ssize_t rc;

	while (left > 0) {
		rc = recv(fd, (caddr_t) ptr + read, left, 0);

		/* File descriptor closed */
		if (rc == 0)
			return ECONNRESET;

		if (rc == -1) {
			if (errno == EINTR)
				continue;
			return errno;
		}

		read += rc;
		left -= rc;
	}

	return 0;
}

int dctl_send_data(int fd, const void *ptr, size_t size)
{
	ssize_t rc;

	do {
		rc = send(fd, ptr, size, MSG_NOSIGNAL);
	} while(rc == -1 && errno == EINTR);

	return rc == size ? 0 : EIO;
}

