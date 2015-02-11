/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license from the top-level
 * OPENSOLARIS.LICENSE or <http://opensource.org/licenses/CDDL-1.0>.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each file
 * and include the License file from the top-level OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049).
 * Copyright (C) 2013-2014 Lawrence Livermore National Security, LLC.
 */

#ifndef	ZED_H
#define	ZED_H

/*
 * Absolute path for the default zed configuration file.
 */
#define	ZED_CONF_FILE		SYSCONFDIR "/zfs/zed.conf"

/*
 * Absolute path for the default zed pid file.
 */
#define	ZED_PID_FILE		RUNSTATEDIR "/zed.pid"

/*
 * Absolute path for the default zed state file.
 */
#define	ZED_STATE_FILE		RUNSTATEDIR "/zed.state"

/*
 * Absolute path for the default zed zedlet directory.
 */
#define	ZED_ZEDLET_DIR		SYSCONFDIR "/zfs/zed.d"

/*
 * Reserved for future use.
 */
#define	ZED_MAX_EVENTS		0

/*
 * Reserved for future use.
 */
#define	ZED_MIN_EVENTS		0

/*
 * String prefix for ZED variables passed via environment variables.
 */
#define	ZED_VAR_PREFIX		"ZED_"

/*
 * String prefix for ZFS event names passed via environment variables.
 */
#define	ZEVENT_VAR_PREFIX	"ZEVENT_"

#endif	/* !ZED_H */
