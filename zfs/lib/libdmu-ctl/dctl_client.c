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
#include <stdlib.h>
#include <string.h>
#include <ftw.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/debug.h>

#include <sys/dmu_ctl.h>
#include <sys/dmu_ctl_impl.h>

/*
 * Try to connect to the socket given in path.
 *
 * For nftw() convenience, returns 0 if unsuccessful, otherwise
 * returns the socket descriptor.
 */
static int try_connect(const char *path)
{
	struct sockaddr_un name;
	int sock;

	sock = socket(PF_UNIX, SOCK_STREAM, 0);
	if (sock == -1) {
		perror("socket");
		return 0;
	}

	/*
	 * The socket fd cannot be 0 otherwise nftw() will not interpret the
	 * return code correctly.
	 */
	VERIFY(sock != 0);

	name.sun_family = AF_UNIX;
	strncpy(name.sun_path, path, sizeof(name.sun_path));

	name.sun_path[sizeof(name.sun_path) - 1] = '\0';

	if (connect(sock, (struct sockaddr *) &name, sizeof(name)) == -1) {
		close(sock);
		return 0;
	}

	return sock;
}

/*
 * nftw() callback.
 */
static int nftw_cb(const char *fpath, const struct stat *sb, int typeflag,
    struct FTW *ftwbuf)
{
	if (!S_ISSOCK(sb->st_mode))
		return 0;

	if (strcmp(&fpath[ftwbuf->base], SOCKNAME) != 0)
		return 0;

	return try_connect(fpath);
}

/*
 * For convenience, if check_subdirs is true we walk the directory tree to
 * find a good socket.
 */
int dctlc_connect(const char *dir, boolean_t check_subdirs)
{
	char *fpath;
	int fd;

	if (check_subdirs)
		fd = nftw(dir, nftw_cb, 10, FTW_PHYS);
	else {
		fpath = malloc(strlen(dir) + strlen(SOCKNAME) + 2);
		if (fpath == NULL)
			return -1;

		strcpy(fpath, dir);
		strcat(fpath, "/" SOCKNAME);

		fd = try_connect(fpath);

		free(fpath);
	}

	return fd == 0 ? -1 : fd;
}

void dctlc_disconnect(int fd)
{
	(void) shutdown(fd, SHUT_RDWR);
}

static int dctl_reply_copyin(int fd, dctl_cmd_t *cmd)
{
	return dctl_send_data(fd, (void *)(uintptr_t) cmd->u.dcmd_copy.ptr,
	    cmd->u.dcmd_copy.size);
}

static int dctl_reply_copyinstr(int fd, dctl_cmd_t *cmd)
{
	dctl_cmd_t reply;
	char *from;
	size_t len, buflen, to_copy;
	int error;

	reply.dcmd_msg = DCTL_GEN_REPLY;

	from = (char *)(uintptr_t) cmd->u.dcmd_copy.ptr;

	buflen = cmd->u.dcmd_copy.size;
	to_copy = strnlen(from, buflen - 1);

	reply.u.dcmd_reply.rc = from[to_copy] == '\0' ? 0 : ENAMETOOLONG;
	reply.u.dcmd_reply.size = to_copy;

	error = dctl_send_msg(fd, &reply);

	if (!error && to_copy > 0)
		error = dctl_send_data(fd, from, to_copy);

	return error;
}

static int dctl_reply_copyout(int fd, dctl_cmd_t *cmd)
{
	return dctl_read_data(fd, (void *)(uintptr_t) cmd->u.dcmd_copy.ptr,
	    cmd->u.dcmd_copy.size);
}

static int dctl_reply_fd_read(int fd, dctl_cmd_t *cmd)
{
	dctl_cmd_t reply;
	void *buf;
	int error;
	ssize_t rrc, size = cmd->u.dcmd_fd_io.size;

	buf = malloc(size);
	if (buf == NULL)
		return ENOMEM;

	rrc = read(cmd->u.dcmd_fd_io.fd, buf, size);

	reply.dcmd_msg = DCTL_GEN_REPLY;
	reply.u.dcmd_reply.rc = rrc == -1 ? errno : 0;
	reply.u.dcmd_reply.size = rrc;

	error = dctl_send_msg(fd, &reply);

	if (!error && rrc > 0)
		error = dctl_send_data(fd, buf, rrc);

out:
	free(buf);

	return error;
}

static int dctl_reply_fd_write(int fd, dctl_cmd_t *cmd)
{
	dctl_cmd_t reply;
	void *buf;
	int error;
	ssize_t wrc, size = cmd->u.dcmd_fd_io.size;

	buf = malloc(size);
	if (buf == NULL)
		return ENOMEM;

	error = dctl_read_data(fd, buf, size);
	if (error)
		goto out;

	wrc = write(cmd->u.dcmd_fd_io.fd, buf, size);

	reply.dcmd_msg = DCTL_GEN_REPLY;
	reply.u.dcmd_reply.rc = wrc == -1 ? errno : 0;
	reply.u.dcmd_reply.size = wrc;

	error = dctl_send_msg(fd, &reply);

out:
	free(buf);

	return error;
}

int dctlc_ioctl(int fd, int32_t request, void *arg)
{
	int error;
	dctl_cmd_t cmd;

	ASSERT(fd != 0);

	cmd.dcmd_msg = DCTL_IOCTL;

	cmd.u.dcmd_ioctl.cmd = request;
	cmd.u.dcmd_ioctl.arg = (uintptr_t) arg;

	error = dctl_send_msg(fd, &cmd);

	while (!error && (error = dctl_read_msg(fd, &cmd)) == 0) {
		switch (cmd.dcmd_msg) {
			case DCTL_IOCTL_REPLY:
				error = cmd.u.dcmd_reply.rc;
				goto out;
			case DCTL_COPYIN:
				error = dctl_reply_copyin(fd, &cmd);
				break;
			case DCTL_COPYINSTR:
				error = dctl_reply_copyinstr(fd, &cmd);
				break;
			case DCTL_COPYOUT:
				error = dctl_reply_copyout(fd, &cmd);
				break;
			case DCTL_FD_READ:
				error = dctl_reply_fd_read(fd, &cmd);
				break;
			case DCTL_FD_WRITE:
				error = dctl_reply_fd_write(fd, &cmd);
				break;
			default:
				fprintf(stderr, "%s(): invalid message "
				    "received.\n", __func__);
				error = EINVAL;
				goto out;
		}
	}

out:
	errno = error;
	return error ? -1 : 0;
}
