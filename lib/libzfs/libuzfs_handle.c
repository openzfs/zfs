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

#include <errno.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <libuzfs.h>
#include "libzfs_impl.h"
#include <sys/un.h>
#include <stdlib.h>

static pthread_t main_thread;

extern int g_fd;

/*
 * check if execution in happening in uzfs thread or not
 */
int
is_main_thread(void)
{
	return (main_thread == pthread_self());
}

/*
 * cleanup zc nvlist
 */
static void inline uzfs_ioctl_done(uzfs_ioctl_t *cmd, zfs_cmd_t *zc)
{
	free((void *) zc->zc_nvlist_src);
	free((void *) zc->zc_nvlist_dst);
	free((void *) zc->zc_nvlist_conf);
	free((void *) zc->zc_history);
}

/*
 * allocate nvlist for zfs command
 */
static int inline uzfs_ioctl_init(uzfs_ioctl_t *cmd, zfs_cmd_t *zc)
{
	void *ptr;

	zc->zc_nvlist_src = zc->zc_nvlist_dst = zc->zc_nvlist_conf =
	    zc->zc_history = (uint64_t)NULL;

	if (zc->zc_nvlist_src_size) {
		ptr = malloc(zc->zc_nvlist_src_size);
		if (ptr == NULL)
			goto err;
		zc->zc_nvlist_src = (uint64_t)ptr;
	}
	if (zc->zc_nvlist_dst_size) {
		ptr = malloc(zc->zc_nvlist_dst_size);
		if (ptr == NULL)
			goto err;
		zc->zc_nvlist_dst = (uint64_t)ptr;
	}
	if (zc->zc_nvlist_conf_size) {
		ptr = malloc(zc->zc_nvlist_conf_size);
		if (ptr == NULL)
			goto err;
		zc->zc_nvlist_conf = (uint64_t)ptr;
	}
	size_t his_size = (cmd->his_len ? cmd->his_len : zc->zc_history_len);
	if (his_size) {
		ptr = malloc(his_size);
		if (ptr == NULL)
			goto err;
		zc->zc_history = (uint64_t)ptr;
	}

	return (0);
err:
	uzfs_ioctl_done(cmd, zc);
	return (-1);
}

/*
 * initialize unix socket and connect to uzfs server,
 * returns file descriptor to send command to uzfs server.
 */
int
uzfs_client_init(const char *sock_path)
{
	int sock;
	struct sockaddr_un server_addr = { 0 };

	if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		return (-1);
	}

	server_addr.sun_family = AF_UNIX;
	strncpy(server_addr.sun_path, sock_path,
	    sizeof (server_addr.sun_path) - 1);

	if (connect(sock, (struct sockaddr *)&server_addr,
	    sizeof (server_addr)) < 0) {
		VERIFY0(close(sock));
		return (-1);
	}
	return (sock);
}

/*
 * initialize uzfs client
 */
int
libuzfs_client_init(libzfs_handle_t *g_zfs)
{
	g_fd = uzfs_client_init(UZFS_SOCK);
	if (g_fd < 0)
		return (-1);
	if (g_zfs)
		g_zfs->libzfs_fd = g_fd;
	main_thread = pthread_self();
	return (0);
}

static int
uzfs_read_packet(int fd, void *ptr, uint64_t size)
{
	uint64_t buf_len = 0, len;
	char *buf = ptr;

	if (0 == size)
		return (1);

	do {
		if ((len = read(fd, buf + buf_len, size - buf_len)) < 0) {
			return (-1);
		}
		buf_len += len;
	} while (len && buf_len < size);

	return (buf_len == size);
}

static int
uzfs_write_packet(int fd, void *ptr, uint64_t size)
{
	uint64_t buf_len = 0, len;
	char *buf = ptr;

	if (0 == size)
		return (1);

	do {
		if ((len = write(fd, buf + buf_len, size - buf_len)) < 0) {
			return (-1);
		}
		buf_len += len;
	} while (buf_len < size);

	return (buf_len == size);
}

/*
 * read the server reponse for the ioctl
 */
int
uzfs_recv_response(int fd, zfs_cmd_t *zc)
{
	uzfs_ioctl_t uzfs_cmd;
	zfs_cmd_t uzc;

	if (uzfs_read_packet(fd, &uzfs_cmd, sizeof (uzfs_cmd)) <= 0)
		return (EPIPE);

	if (uzfs_read_packet(fd, &uzc, sizeof (uzc)) <= 0)
		return (EPIPE);

	/*
	 * Ideal way to do this is reorganise zfs_cmd_t structure and
	 * copy from the memory offset. Doing it the dirty way so that
	 * the binary is backward compatible.
	 */
	/* backup the pointers */
	uint64_t src = zc->zc_nvlist_src;
	uint64_t dst = zc->zc_nvlist_dst;
	uint64_t conf = zc->zc_nvlist_conf;
	uint64_t his = zc->zc_history;

	*zc = uzc;

	/* restore the pointers */
	zc->zc_nvlist_src = src;
	zc->zc_nvlist_dst = dst;
	zc->zc_nvlist_conf = conf;
	zc->zc_history = his;

	if (zc->zc_history && zc->zc_history_len &&
	    uzfs_read_packet(fd, (void *) zc->zc_history,
	    zc->zc_history_len) <= 0)
		return (EPIPE);

	if (uzc.zc_nvlist_dst_filled &&
	    uzfs_read_packet(fd, (void *) zc->zc_nvlist_dst,
	    zc->zc_nvlist_dst_size) <= 0)
		return (EPIPE);

	return (uzfs_cmd.ioc_ret);
}

/*
 * send ioctl request to uzfs server.
 * return 0 if request is successfully sent else -1
 */
int
uzfs_send_ioctl(int fd, unsigned long request, zfs_cmd_t *zc)
{
	uzfs_ioctl_t uzfs_cmd = {0};

	uzfs_cmd.ioc_num = request;

	if (!zc->zc_history_len && zc->zc_history)
		uzfs_cmd.his_len = strlen((char *)zc->zc_history);

	char *src = (char *)zc->zc_nvlist_src;
	char *conf = (char *)zc->zc_nvlist_conf;
	char *his = (char *)zc->zc_history;

	uzfs_cmd.packet_size = (sizeof (uzfs_ioctl_t) + sizeof (zfs_cmd_t) +
	    zc->zc_nvlist_src_size + zc->zc_nvlist_conf_size
	    + uzfs_cmd.his_len);

	if (uzfs_write_packet(fd, &uzfs_cmd, sizeof (uzfs_ioctl_t)) <= 0)
		return (-1);

	if (uzfs_write_packet(fd, zc, sizeof (zfs_cmd_t)) <= 0)
		return (-1);

	if (uzfs_write_packet(fd, src, zc->zc_nvlist_src_size) <= 0)
		return (-1);

	if (uzfs_write_packet(fd, conf, zc->zc_nvlist_conf_size) <= 0)
		return (-1);

	if (uzfs_write_packet(fd, his, uzfs_cmd.his_len) <= 0)
		return (-1);

	return (0);
}

/*
 * receive ioctl request from client
 */
int
uzfs_recv_ioctl(int fd, zfs_cmd_t *zc, uzfs_info_t *ucmd_info)
{
	uzfs_ioctl_t *uzfs_cmd = &ucmd_info->uzfs_cmd;
	ucmd_info->uzfs_recvfd = -1;

	if (uzfs_read_packet(fd, uzfs_cmd, sizeof (uzfs_ioctl_t)) <= 0)
		return (-1);

	if (uzfs_read_packet(fd, zc, sizeof (zfs_cmd_t)) <= 0)
		return (-1);

	if (uzfs_ioctl_init(uzfs_cmd, zc) < 0)
		return (-1);

	if (uzfs_read_packet(fd, (void *) zc->zc_nvlist_src,
	    zc->zc_nvlist_src_size) <= 0)
		goto err;

	if (uzfs_read_packet(fd, (void *) zc->zc_nvlist_conf,
	    zc->zc_nvlist_conf_size) <= 0)
		goto err;

	if (uzfs_read_packet(fd, (void *) zc->zc_history, uzfs_cmd->his_len) <=
	    0)
		goto err;

	return (0);
err:
	uzfs_ioctl_done(uzfs_cmd, zc);
	return (-1);
}

/*
 * send ioctl reponse to client
 */
int
uzfs_send_response(int fd, zfs_cmd_t *zc, uzfs_info_t *ucmd_info)
{
	uzfs_ioctl_t *uzfs_cmd = &ucmd_info->uzfs_cmd;
	int err = -1;
	if ((uzfs_cmd->ioc_num == ZFS_IOC_SEND && !zc->zc_guid) ||
	    uzfs_cmd->ioc_num == ZFS_IOC_RECV ||
	    uzfs_cmd->ioc_num == ZFS_IOC_RECV_NEW ||
	    uzfs_cmd->ioc_num == ZFS_IOC_SEND_NEW) {
		VERIFY0(close(ucmd_info->uzfs_recvfd));
	}

	uzfs_cmd->packet_size =
	    (zc->zc_nvlist_dst_filled ? zc->zc_nvlist_dst_size : 0);

	uzfs_cmd->packet_size +=
	    (sizeof (uzfs_ioctl_t) + sizeof (zfs_cmd_t) + zc->zc_history_len);

	if (uzfs_write_packet(fd, uzfs_cmd, sizeof (uzfs_ioctl_t)) <= 0) {
		goto out;
	}

	if (uzfs_write_packet(fd, zc, sizeof (zfs_cmd_t)) <= 0) {
		goto out;
	}

	if (uzfs_write_packet(fd, (void *) zc->zc_history,
	    zc->zc_history_len) <= 0) {
		goto out;
	}

	char *buf = (char *)zc->zc_nvlist_dst;

	if (zc->zc_nvlist_dst_filled &&
	    uzfs_write_packet(fd, buf, zc->zc_nvlist_dst_size) <= 0) {
		goto out;
	}
	err = 0;
out:
	uzfs_ioctl_done(uzfs_cmd, zc);
	return (err);
}
