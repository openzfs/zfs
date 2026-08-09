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

#ifndef	ZED_CONF_H
#define	ZED_CONF_H

#include <libzfs.h>
#include <stdint.h>
#include "zed_strings.h"

struct zed_conf {
	char		*pid_file;		/* abs path to pid file */
	char		*zedlet_dir;		/* abs path to zedlet dir */
	char		*state_file;		/* abs path to state file */

	libzfs_handle_t	*zfs_hdl;		/* handle to libzfs */
	zed_strings_t	*zedlets;		/* names of enabled zedlets */
	char		*path;		/* custom $PATH for zedlets to use */

	int		pid_fd;			/* fd to pid file for lock */
	int		state_fd;		/* fd to state file */
	int		zevent_fd;		/* fd for access to zevents */

	int16_t max_jobs;		/* max zedlets to run at one time */
	int32_t max_zevent_buf_len;	/* max size of kernel event list */

	boolean_t	do_force:1;		/* true if force enabled */
	boolean_t	do_foreground:1;	/* true if run in foreground */
	boolean_t	do_memlock:1;		/* true if locking memory */
	boolean_t	do_verbose:1;		/* true if verbosity enabled */
	boolean_t	do_zero:1;		/* true if zeroing state */
	boolean_t	do_idle:1;		/* true if idle enabled */
};

void zed_conf_init(struct zed_conf *zcp);
void zed_conf_destroy(struct zed_conf *zcp);

void zed_conf_parse_opts(struct zed_conf *zcp, int argc, char **argv);

int zed_conf_scan_dir(struct zed_conf *zcp);

int zed_conf_write_pid(struct zed_conf *zcp);

int zed_conf_open_state(struct zed_conf *zcp);
int zed_conf_read_state(struct zed_conf *zcp, uint64_t *eidp, int64_t etime[]);
int zed_conf_write_state(struct zed_conf *zcp, uint64_t eid, int64_t etime[]);

#endif	/* !ZED_CONF_H */
