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
 * Copyright 2015 OpenZFS on OS X. All rights reserved.
 */

#ifndef ZCONFIGD_H
#define	ZCONFIGD_H

#ifdef  __cplusplus
extern "C" {
#endif

#define	ZSYSCTL_CMD_PATH		SBINDIR "/zsysctl"
#define	ZSYSCTL_CONF_FILE		SYSCONFDIR "/zfs/zsysctl.conf"
#define	ZSYSCTL_CMD_WITH_ARGS		ZSYSCTL_CMD_PATH" -f "ZSYSCTL_CONF_FILE

#define	kNetLundmanZfsZvol		"osx_openzfsonosx_zfs_zvol"

#ifdef  __cplusplus
}
#endif

#endif  /* ZCONFIGD_H */
