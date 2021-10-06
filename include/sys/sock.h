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

#ifndef _SYS_SOCK_H
#define	_SYS_SOCK_H

#include <sys/fcntl.h>
#ifdef _KERNEL
#include <linux/un.h>
#include <linux/net.h>
#else
#include <sys/un.h>
#include <sys/socket.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif


#ifdef _KERNEL

#define	INVALID_SOCKET (NULL)
#define	SOCK_FMT "%px"

typedef struct socket *ksocket_t;
typedef struct kvec kvec_t;

#else /* !_KERNEL */

#define	INVALID_SOCKET (-1)
#define	SOCK_FMT "%d"

typedef int ksocket_t;
typedef struct iovec kvec_t;

#endif /* _KERNEL */

int ksock_create(int domain, int type, int protocol, ksocket_t *sock);
int ksock_connect(ksocket_t sock, struct sockaddr *socket_address,
    unsigned long socklen);
void ksock_close(ksocket_t sock);
int ksock_shutdown(ksocket_t sock, int how);
size_t ksock_send(ksocket_t sock, struct msghdr *msg, kvec_t *iov, int iovcnt,
    int total_size);
size_t ksock_receive(ksocket_t sock, struct msghdr *msg, kvec_t *iov,
    int iovcnt, int total_size, int flags);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SOCK_H */
