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

#ifndef	ZED_CONF_H
#define	ZED_CONF_H

#include <libzfs.h>
#include <stdint.h>
#include "zed_strings.h"

struct zed_conf {
	unsigned	do_force:1;		/* true if force enabled */
	unsigned	do_foreground:1;	/* true if run in foreground */
	unsigned	do_memlock:1;		/* true if locking memory */
	unsigned	do_verbose:1;		/* true if verbosity enabled */
	unsigned	do_zero:1;		/* true if zeroing state */
	int		syslog_facility;	/* syslog facility value */
	int		min_events;		/* RESERVED FOR FUTURE USE */
	int		max_events;		/* RESERVED FOR FUTURE USE */
	char		*conf_file;		/* abs path to config file */
	char		*pid_file;		/* abs path to pid file */
	int		pid_fd;			/* fd to pid file for lock */
	char		*zedlet_dir;		/* abs path to zedlet dir */
	zed_strings_t	*zedlets;		/* names of enabled zedlets */
	char		*state_file;		/* abs path to state file */
	int		state_fd;		/* fd to state file */
	libzfs_handle_t	*zfs_hdl;		/* handle to libzfs */
	int		zevent_fd;		/* fd for access to zevents */
};

struct zed_conf *zed_conf_create(void);

void zed_conf_destroy(struct zed_conf *zcp);

void zed_conf_parse_opts(struct zed_conf *zcp, int argc, char **argv);

void zed_conf_parse_file(struct zed_conf *zcp);

int zed_conf_scan_dir(struct zed_conf *zcp);

int zed_conf_write_pid(struct zed_conf *zcp);

int zed_conf_open_state(struct zed_conf *zcp);

int zed_conf_read_state(struct zed_conf *zcp, uint64_t *eidp, int64_t etime[]);

int zed_conf_write_state(struct zed_conf *zcp, uint64_t eid, int64_t etime[]);

#endif	/* !ZED_CONF_H */
