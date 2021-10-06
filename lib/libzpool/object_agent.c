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

#include <stdio.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <object_agent.h>

extern struct sockaddr_un zfs_root_socket;
extern struct sockaddr_un zfs_public_socket;

void
set_object_agent_sock_dir(char *sock_dir)
{
	snprintf(zfs_root_socket.sun_path,
	    sizeof (zfs_root_socket.sun_path),
	    "%s/zfs_root_socket", sock_dir);
	snprintf(zfs_public_socket.sun_path,
	    sizeof (zfs_public_socket.sun_path),
	    "%s/zfs_public_socket", sock_dir);
}
