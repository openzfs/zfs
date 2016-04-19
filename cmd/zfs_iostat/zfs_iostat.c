#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <linux/socket.h>
#include <sys/epoll.h>

#include "libioacct.h"

int
open_netlink(void)
{
	int netlink_socket;
	int netlink_group = ZFS_NL_IO_GRP;
	struct sockaddr_nl netlink_addr;

	memset(&netlink_addr, 0, sizeof (netlink_addr));
	netlink_addr.nl_family = AF_NETLINK;
	netlink_addr.nl_groups = ZFS_NL_IO_GRP;
	netlink_socket = socket(AF_NETLINK, SOCK_RAW, ZFS_NL_IO_PROTO);

	if (bind(netlink_socket, (struct sockaddr *) &netlink_addr,
		sizeof (netlink_addr)) < 0) {
		printf("bind() unsuccessfull: %s.\n", strerror(errno));
		return (-1);
	}

	/*
	 * Use '270' vs. SOL_NETLINK below, due to strange error
	 * linux/socket.h:318:
	 * #define SOL_NETLINK	270
	 * When use this macro compile fails:
	 * 'zfs_iostat.c:35:19: error: 'SOL_NETLINK' undeclared'
	 */
	if (setsockopt(
		netlink_socket, 270, NETLINK_ADD_MEMBERSHIP,
		&netlink_group, sizeof (netlink_group)) < 0) {
		printf("setsockopt() unsuccessfull: %s.\n", strerror(errno));
		return (-1);
	}

	return (netlink_socket);
}

void
read_event(int sock)
{
	struct iovec iov;
	struct msghdr msgbuf;
	struct nlmsghdr *nl_header;
	nl_msg *io_msg;
	zfs_io_info_t zii;
	char zfs_op[3];

	io_msg = malloc(NETLINK_MSGLEN);
	if (!io_msg) {
		printf("malloc() unsuccessfull: %s\n.", strerror(errno));
		return;
	}
	nl_header = (struct nlmsghdr *)malloc(NLMSG_SPACE(NETLINK_MSGLEN));
	if (!nl_header) {
		printf("malloc() unsuccessfull: %s\n.", strerror(errno));
		return;
	}

	memset(nl_header, 0, NLMSG_SPACE(NETLINK_MSGLEN));
	nl_header->nlmsg_len = NLMSG_SPACE(NETLINK_MSGLEN);

	iov.iov_base = (void *)nl_header;
	iov.iov_len = NLMSG_SPACE(NETLINK_MSGLEN);
	msgbuf.msg_name = NULL;
	msgbuf.msg_namelen = 0;
	msgbuf.msg_iov = &iov;
	msgbuf.msg_iovlen = 1;
	msgbuf.msg_control = NULL;
	msgbuf.msg_controllen = 0;
	msgbuf.msg_flags = 0;

	if (recvmsg(sock, &msgbuf, 0) <= 0) {
		printf("recvmsg() unsuccessfull: %s\n.", strerror(errno));
		return;
	}

	memcpy(io_msg, NLMSG_DATA(nl_header), NETLINK_MSGLEN);
	deserialize_io_info(&zii, io_msg);
	free(io_msg);
	free(nl_header);

	switch (zii.op) {
		case ZFS_NL_READ:
			strncpy(zfs_op, "cr\0", 3);
			break;
		case ZFS_NL_WRITE:
			strncpy(zfs_op, "cw\0", 3);
			break;
		case ZFS_NL_READPAGE:
			strncpy(zfs_op, "mr\0", 3);
			break;
		case ZFS_NL_WRITEPAGE:
			strncpy(zfs_op, "mw\0", 3);
			break;
		default:
			strcpy(zfs_op, "--");
	}
	printf("%s %d %zd %s\n", zii.fsname, zii.pid, zii.nbytes, zfs_op);
}

int
main(int argc, char *argv[])
{
	int epoll_fd, netlink_socket, nr_events;
	struct epoll_event event = {
		.events = 0
	};

	netlink_socket = open_netlink();
	if (netlink_socket < 0)
		return (netlink_socket);

	epoll_fd = epoll_create1(0);
	if (epoll_fd == -1) {
		printf("epoll_create() unsuccessfull: %s.\n", strerror(errno));
		return (-1);
	}

	event.data.fd = netlink_socket;
	event.events = EPOLLIN;
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, netlink_socket, &event) == -1) {
		printf("epoll_ctl() unsuccessfull: %s.\n", strerror(errno));
		return (-1);
	}

	while (1) {
		nr_events = epoll_wait(epoll_fd, &event, 1, -1);
		read_event(event.data.fd);
	}

	return (0);
}
