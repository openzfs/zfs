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
 * Copyright (c) 2021 by Delphix. All rights reserved.
 */

#include <sys/abd.h>
#include <sys/sock.h>

#ifdef _KERNEL
int
ksock_create(int domain, int type, int protocol, ksocket_t *sock)
{
	return (sock_create(domain, type, protocol, sock));
}

int
ksock_connect(ksocket_t sock, struct sockaddr *socket_address,
    unsigned long socklen)
{
	return (sock->ops->connect(sock, socket_address, socklen, 0));
}

void
ksock_close(ksocket_t sock)
{
	sock_release(sock);
}

int
ksock_shutdown(ksocket_t sock, int how)
{
	return (kernel_sock_shutdown(sock, how));
}

size_t
ksock_send(ksocket_t sock, struct msghdr *msg, kvec_t *iov,
    int iovcnt, int total_size)
{
	return (kernel_sendmsg(sock, msg, iov, iovcnt, total_size));
}

size_t
ksock_receive(ksocket_t sock, struct msghdr *msg, kvec_t *iov,
    int iovcnt, int total_size, int flags)
{
	return (kernel_recvmsg(sock, msg, iov, iovcnt, total_size, flags));
}

#else /* !_KERNEL */
int
ksock_create(int domain, int type, int protocol, ksocket_t *sock)
{
	*sock = socket(PF_UNIX, type, protocol);
	if (*sock == -1) {
		return (errno);
	}
	return (0);
}

int
ksock_connect(ksocket_t sock, struct sockaddr *socket_address,
    unsigned long socklen)
{
	return (connect(sock, socket_address, socklen));
}

void
ksock_close(ksocket_t sock)
{
	close(sock);
}

int
ksock_shutdown(ksocket_t sock, int how)
{
	return (shutdown(sock, how));
}

size_t
ksock_send(ksocket_t sock, struct msghdr *msg, kvec_t *iov,
    int iovcnt, int total_size)
{
	return (writev(sock, iov, iovcnt));
}

size_t
ksock_receive(ksocket_t sock, struct msghdr *msg, kvec_t *iov,
    int iovcnt, int total_size, int flags)
{
	return (readv(sock, iov, iovcnt));
}

#endif /* _KERNEL */
