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
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <limits.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/debug.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/list.h>
#include <sys/cred.h>

#include <sys/dmu_ctl.h>
#include <sys/dmu_ctl_impl.h>

static dctl_sock_info_t ctl_sock = {
	.dsi_mtx = PTHREAD_MUTEX_INITIALIZER,
	.dsi_fd = -1
};

static int dctl_create_socket_common();

/*
 * Routines from zfs_ioctl.c
 */
extern int zfs_ioctl_init(void);
extern int zfs_ioctl_fini(void);
extern int zfsdev_ioctl(dev_t dev, int cmd, intptr_t arg, int flag, cred_t *cr,
    int *rvalp);

/*
 * We can't simply put the client file descriptor in wthr_info_t because we
 * have no way of accessing it from the DMU code without extensive
 * modifications.
 *
 * Therefore each worker thread will have it's own global thread-specific
 * client_fd variable.
 */
static __thread int client_fd = -1;

int dctls_copyin(const void *src, void *dest, size_t size)
{
	dctl_cmd_t cmd;

	VERIFY(client_fd >= 0);

	cmd.dcmd_msg = DCTL_COPYIN;
	cmd.u.dcmd_copy.ptr = (uintptr_t) src;
	cmd.u.dcmd_copy.size = size;

	if (dctl_send_msg(client_fd, &cmd) != 0)
		return EFAULT;

	if (dctl_read_data(client_fd, dest, size) != 0)
		return EFAULT;

	return 0;
}

int dctls_copyinstr(const char *from, char *to, size_t max, size_t *len)
{
	dctl_cmd_t msg;
	size_t copied;

	VERIFY(client_fd >= 0);

	if (max == 0)
		return ENAMETOOLONG;
	if (max < 0)
		return EFAULT;

	msg.dcmd_msg = DCTL_COPYINSTR;
	msg.u.dcmd_copy.ptr = (uintptr_t) from;
	msg.u.dcmd_copy.size = max;

	if (dctl_send_msg(client_fd, &msg) != 0)
		return EFAULT;

	if (dctl_read_msg(client_fd, &msg) != 0)
		return EFAULT;

	if (msg.dcmd_msg != DCTL_GEN_REPLY)
		return EFAULT;

	copied = msg.u.dcmd_reply.size;

	if (copied >= max)
		return EFAULT;

	if (copied > 0)
		if (dctl_read_data(client_fd, to, copied) != 0)
			return EFAULT;

	to[copied] = '\0';

	if (len != NULL)
		*len = copied + 1;

	return msg.u.dcmd_reply.rc;
}

int dctls_copyout(const void *src, void *dest, size_t size)
{
	dctl_cmd_t cmd;

	VERIFY(client_fd >= 0);

	cmd.dcmd_msg = DCTL_COPYOUT;
	cmd.u.dcmd_copy.ptr = (uintptr_t) dest;
	cmd.u.dcmd_copy.size = size;

	if (dctl_send_msg(client_fd, &cmd) != 0)
		return EFAULT;

	if (dctl_send_data(client_fd, src, size) != 0)
		return EFAULT;

	return 0;
}

int dctls_fd_read(int fd, void *buf, ssize_t len, ssize_t *residp)
{
	dctl_cmd_t msg;
	uint64_t dsize;
	int error;

	VERIFY(client_fd >= 0);

	msg.dcmd_msg = DCTL_FD_READ;
	msg.u.dcmd_fd_io.fd = fd;
	msg.u.dcmd_fd_io.size = len;

	if ((error = dctl_send_msg(client_fd, &msg)) != 0)
		return error;

	if ((error = dctl_read_msg(client_fd, &msg)) != 0)
		return error;

	if (msg.dcmd_msg != DCTL_GEN_REPLY)
		return EIO;

	if (msg.u.dcmd_reply.rc != 0)
		return msg.u.dcmd_reply.rc;

	dsize = msg.u.dcmd_reply.size;

	if (dsize > 0)
		error = dctl_read_data(client_fd, buf, dsize);

	*residp = len - dsize;

	return error;
}

int dctls_fd_write(int fd, const void *src, ssize_t len)
{
	dctl_cmd_t msg;
	int error;

	VERIFY(client_fd >= 0);

	msg.dcmd_msg = DCTL_FD_WRITE;
	msg.u.dcmd_fd_io.fd = fd;
	msg.u.dcmd_fd_io.size = len;

	error = dctl_send_msg(client_fd, &msg);

	if (!error)
		error = dctl_send_data(client_fd, src, len);

	if (!error)
		error = dctl_read_msg(client_fd, &msg);

	if (error)
		return error;

	if (msg.dcmd_msg != DCTL_GEN_REPLY)
		return EIO;

	if (msg.u.dcmd_reply.rc != 0)
		return msg.u.dcmd_reply.rc;

	/*
	 * We have to do this because the original upstream code
	 * does not check if residp == len.
	 */
	if (msg.u.dcmd_reply.size != len)
		return EIO;

	return 0;
}

/* Handle a new connection */
static void dctl_handle_conn(int sock_fd)
{
	dctl_cmd_t cmd;
	dev_t dev = { 0 };
	int rc;

	client_fd = sock_fd;

	while (dctl_read_msg(sock_fd, &cmd) == 0) {
		if (cmd.dcmd_msg != DCTL_IOCTL) {
			fprintf(stderr, "%s(): unexpected message type.\n",
			    __func__);
			break;
		}

		rc = zfsdev_ioctl(dev, cmd.u.dcmd_ioctl.cmd,
		    (intptr_t) cmd.u.dcmd_ioctl.arg, 0, NULL, NULL);

		cmd.dcmd_msg = DCTL_IOCTL_REPLY;
		cmd.u.dcmd_reply.rc = rc;

		if (dctl_send_msg(sock_fd, &cmd) != 0)
			break;
	}
	close(sock_fd);

	client_fd = -1;
}

/* Main worker thread loop */
static void *dctl_thread(void *arg)
{
	wthr_info_t *thr = arg;
	struct pollfd fds[1];

	fds[0].events = POLLIN;

	pthread_mutex_lock(&ctl_sock.dsi_mtx);

	while (!thr->wthr_exit) {
		/* Clean-up dead threads */
		dctl_thr_join();

		/* The file descriptor might change in the thread lifetime */
		fds[0].fd = ctl_sock.dsi_fd;

		/* Poll socket with 1-second timeout */
		int rc = poll(fds, 1, 1000);
		if (rc == 0 || (rc == -1 && errno == EINTR))
			continue;

		/* Recheck the exit flag */
		if (thr->wthr_exit)
			break;

		if (rc == -1) {
			/* Unknown error, let's try to recreate the socket */
			close(ctl_sock.dsi_fd);
			ctl_sock.dsi_fd = -1;

			if (dctl_create_socket_common() != 0)
				break;

			continue;
		}
		ASSERT(rc == 1);

		short rev = fds[0].revents;
		if (rev == 0)
			continue;
		ASSERT(rev == POLLIN);

		/*
		 * At this point there should be a connection ready to be
		 * accepted.
		 */
		int client_fd = accept(ctl_sock.dsi_fd, NULL, NULL);
		/* Many possible errors here, we'll just retry */
		if (client_fd == -1)
			continue;

		/*
		 * Now lets handle the request. This can take a very
		 * long time (hours even), so we'll let other threads
		 * handle new connections.
		 */
		pthread_mutex_unlock(&ctl_sock.dsi_mtx);

		dctl_thr_rebalance(thr, B_FALSE);
		dctl_handle_conn(client_fd);
		dctl_thr_rebalance(thr, B_TRUE);

		pthread_mutex_lock(&ctl_sock.dsi_mtx);
	}
	pthread_mutex_unlock(&ctl_sock.dsi_mtx);

	dctl_thr_die(thr);

	return NULL;
}

static int dctl_create_socket_common()
{
	dctl_sock_info_t *s = &ctl_sock;
	size_t size;
	int error;

	ASSERT(s->dsi_fd == -1);

	/*
	 * Unlink old socket, in case it exists.
	 * We don't care about errors here.
	 */
	unlink(s->dsi_path);

	/* Create the socket */
	s->dsi_fd = socket(PF_UNIX, SOCK_STREAM, 0);
	if (s->dsi_fd == -1) {
		error = errno;
		perror("socket");
		return error;
	}

	s->dsi_addr.sun_family = AF_UNIX;

	size = sizeof(s->dsi_addr.sun_path) - 1;
	strncpy(s->dsi_addr.sun_path, s->dsi_path, size);

	s->dsi_addr.sun_path[size] = '\0';

	if (bind(s->dsi_fd, (struct sockaddr *) &s->dsi_addr,
	    sizeof(s->dsi_addr)) != 0) {
		error = errno;
		perror("bind");
		return error;
	}

	if (listen(s->dsi_fd, LISTEN_BACKLOG) != 0) {
		error = errno;
		perror("listen");
		unlink(s->dsi_path);
		return error;
	}

	return 0;
}

static int dctl_create_socket(const char *cfg_dir)
{
	int error;
	dctl_sock_info_t *s = &ctl_sock;

	ASSERT(s->dsi_path == NULL);
	ASSERT(s->dsi_fd == -1);

	int pathsize = strlen(cfg_dir) + strlen(SOCKNAME) + 2;
	if (pathsize > sizeof(s->dsi_addr.sun_path))
		return ENAMETOOLONG;

	s->dsi_path = malloc(pathsize);
	if (s->dsi_path == NULL)
		return ENOMEM;

	strcpy(s->dsi_path, cfg_dir);
	strcat(s->dsi_path, "/" SOCKNAME);

	/*
	 * For convenience, create the directory in case it doesn't exist.
	 * We don't care about errors here.
	 */
	mkdir(cfg_dir, 0770);

	error = dctl_create_socket_common();

	if (error) {
		free(s->dsi_path);

		if (s->dsi_fd != -1) {
			close(s->dsi_fd);
			s->dsi_fd = -1;
		}
	}

	return error;
}

static void dctl_destroy_socket()
{
	dctl_sock_info_t *s = &ctl_sock;

	ASSERT(s->dsi_path != NULL);
	ASSERT(s->dsi_fd != -1);

	close(s->dsi_fd);
	s->dsi_fd = -1;

	unlink(s->dsi_path);
	free(s->dsi_path);
}

/*
 * Initialize the DMU userspace control interface.
 * This should be called after kernel_init().
 *
 * Note that only very rarely we have more than a couple of simultaneous
 * lzfs/lzpool connections. Since the thread pool grows automatically when all
 * threads are busy, a good value for min_thr and max_free_thr is 2.
 */
int dctl_server_init(const char *cfg_dir, int min_thr, int max_free_thr)
{
	int error;

	ASSERT(min_thr > 0);
	ASSERT(max_free_thr >= min_thr);

	error = zfs_ioctl_init();
	if (error)
		return error;

	error = dctl_create_socket(cfg_dir);
	if (error) {
		(void) zfs_ioctl_fini();
		return error;
	}

	error = dctl_thr_pool_create(min_thr, max_free_thr, dctl_thread);
	if (error) {
		(void) zfs_ioctl_fini();
		dctl_destroy_socket();
		return error;
	}

	return 0;
}

/*
 * Terminate control interface.
 * This should be called after closing all objsets, but before calling
 * kernel_fini().
 * May return EBUSY if the SPA is busy.
 *
 * Thread pool destruction can take a while due to poll()
 * timeout or due to a thread being busy (e.g. a backup is being taken).
 */
int dctl_server_fini()
{
	dctl_thr_pool_stop();
	dctl_destroy_socket();

	return zfs_ioctl_fini();
}
