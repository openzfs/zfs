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
/*
 * This file is part of the ZFS Event Daemon (ZED).
 *
 * Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049).
 * Copyright (C) 2013-2014 Lawrence Livermore National Security, LLC.
 */

#ifndef	ZED_H
#define	ZED_H

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
 * String prefix for ZED variables passed via environment variables.
 */
#define	ZED_VAR_PREFIX		"ZED_"

/*
 * String prefix for ZFS event names passed via environment variables.
 */
#define	ZEVENT_VAR_PREFIX	"ZEVENT_"

#endif	/* !ZED_H */
