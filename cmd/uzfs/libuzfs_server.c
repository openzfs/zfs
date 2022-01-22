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

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <libuzfs.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/zfs_context.h>
#include <unistd.h>

void close_client(int sock, void *arg);

/*
 * Block SIGPIPE signal
 */
static int
uzfs_server_init(void)
{
	sigset_t set;
	if (sigemptyset(&set) < 0)
		return (-1);
	if (sigaddset(&set, SIGPIPE) < 0)
		return (-1);
	if (pthread_sigmask(SIG_BLOCK, &set, NULL))
		return (-1);
	return (0);
}

/*
 * copy sock fd to *arg and close it
 */
void
close_client(int sock, void *arg)
{
	int64_t fd = (int64_t)arg;
	/*
	 * have to close the fd and also have to make sure this
	 * fd does not get assigned. Both should be happening atomically.
	 */
	if (dup2(sock, fd) < 0) {
		perror("close_client dup2");
	}
}

/*
 * monitor the given socket and execute the relevant action
 */
static void *
uzfs_monitor_socket(void *arg)
{
	uzfs_mon_t *mon = arg;
	int sock = mon->mon_fd;
	int count = 0;

	while (1) {
		char c;
		int x = recv(sock, &c, sizeof (c), MSG_PEEK);
		if (x == 0) {
			mon->mon_action(sock, mon->mon_arg);
			break;
		}
		count++;
		sleep(1);
	}

	pthread_exit(NULL);
}

/*
 * once control reaches to our uZFS process, it does not know anything
 * about the client. So if we did ctrl + c or killed the zfs process,
 * the server will still continue to do zfs send/recv operation.
 * The monitoring framework monitors that and handles that scenario gracefully.
 */
static uzfs_mon_t *
uzfs_monitor_client(int fd, uzfs_info_t *ucmd_info)
{
	uzfs_ioctl_t *uzfs_cmd = &ucmd_info->uzfs_cmd;

	if (ucmd_info->uzfs_recvfd < 0)
		return (NULL);

	uzfs_mon_t *mon = malloc(sizeof (uzfs_mon_t));
	if (mon == NULL)
		return (NULL);

	mon->mon_fd = fd;
	mon->mon_action = close_client;
	mon->mon_arg = (void *) (uint64_t)(ucmd_info->uzfs_recvfd);
	mon->mon_reserved = uzfs_cmd->ioc_num;

	if (pthread_create(&mon->mon_tid, NULL, uzfs_monitor_socket, mon) <
	    0) {
		perror("pthread_create");
		free(mon);
		return (NULL);
	}
	return (mon);
}

/*
 * stop monitoring the given action
 */
static void
uzfs_stop_monitoring(uzfs_mon_t *mon)
{
	if (mon == NULL)
		return;

	pthread_cancel(mon->mon_tid);

	VERIFY0(pthread_join(mon->mon_tid, NULL));
	free(mon);
}

/*
 * process the ioctl from client and send the response
 */
static void
uzfs_process_ioctl(void *arg)
{
	int cfd;
	uzfs_info_t ucmd_info;
	zfs_cmd_t zc;
	int count = 0;
	char *pool = NULL;

	cfd = (int64_t)arg;

	while (1) {
		if (uzfs_recv_ioctl(cfd, &zc, &ucmd_info) < 0)
			break;

		/* legacy ioctls can modify zc_name */
		if (zc.zc_name[0] &&
		    is_config_command(ucmd_info.uzfs_cmd.ioc_num)) {
			if (pool)
				kmem_strfree(pool);
			pool = strdup(zc.zc_name);
			if (pool) {
				pool[strcspn(pool, "/@#")] = '\0';
			}
		}

		uzfs_mon_t *mon = uzfs_monitor_client(cfd, &ucmd_info);

		int ret = uzfs_handle_ioctl(pool, &zc, &ucmd_info);

		ucmd_info.uzfs_cmd.ioc_ret = (ret < 0 ? errno : ret);

		uzfs_stop_monitoring(mon);

		if (uzfs_send_response(cfd, &zc, &ucmd_info) < 0)
			break;

		count++;
	}

	if (pool)
		kmem_strfree(pool);

	close(cfd);
	thread_exit();
}

/*
 * creates server which listens on unix domain socket
 * and process client request
 */
int
libuzfs_run_ioctl_server(void)
{
	int server_s;
	struct sockaddr_un server_addr = { 0 };

	if ((server_s = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		return (-1);

	int x = 1;
	if (setsockopt(server_s, SOL_SOCKET, SO_REUSEADDR, &x, sizeof (x)) <
	    0) {
		goto err;
	}

	struct linger so_linger;
	so_linger.l_onoff = 1;
	so_linger.l_linger = 30;
	if (setsockopt(server_s, SOL_SOCKET, SO_LINGER, &so_linger,
	    sizeof (so_linger))) {
		goto err;
	}

	if (uzfs_server_init() < 0)
		goto err;

	server_addr.sun_family = AF_UNIX;
	strncpy(server_addr.sun_path, UZFS_SOCK,
	    sizeof (server_addr.sun_path) - 1);

	struct stat st;
	if (stat(server_addr.sun_path, &st) == 0) {
		if (unlink(server_addr.sun_path) == -1)
			goto err;
	}

	if (bind(server_s, (struct sockaddr *)&server_addr,
	    sizeof (server_addr)) < 0) {
		goto err;
	}

	if (listen(server_s, PEND_CONNECTIONS) < 0) {
		goto err;
	}

	struct sockaddr_in client_addr;
	unsigned int addr_len = sizeof (client_addr);

	/* accept connection and process it */
	while (1) {
		int cfd;
		if ((cfd = accept(server_s, (struct sockaddr *)&client_addr,
		    &addr_len)) < 0) {
			perror("accept");
			continue;
		}

		VERIFY(thread_create(NULL, 0, uzfs_process_ioctl,
		    (void *)(int64_t)cfd, 0, NULL, TS_RUN,
		    defclsyspri) != NULL);
	}

err:
	VERIFY0(close(server_s));
	return (-1);
}
