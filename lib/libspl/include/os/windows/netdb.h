// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

#ifndef _LIBSPL_NETDB_H
#define	_LIBSPL_NETDB_H

#include <sys/cdefs.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

#define	EAI_ADDRFAMILY	1 /* address family for hostname not supported */

#define	EAI_SYSTEM	11 /* system error returned in errno */
#define	EAI_BADHINTS	12 /* invalid value for hints */
#define	EAI_PROTOCOL	13 /* resolved protocol is unknown */
#define	EAI_OVERFLOW	14 /* argument buffer overflow */
#define	EAI_MAX		15

#define	poll WSAPoll
#define	INFTIM		(-1)

extern ssize_t writev(int fd, struct iovec *iov, unsigned iov_cnt);

#endif
