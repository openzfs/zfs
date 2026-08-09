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

#ifndef	ZED_EVENT_H
#define	ZED_EVENT_H

#include <stdint.h>

int zed_event_init(struct zed_conf *zcp);

void zed_event_fini(struct zed_conf *zcp);

int zed_event_seek(struct zed_conf *zcp, uint64_t saved_eid,
    int64_t saved_etime[]);

int zed_event_service(struct zed_conf *zcp);

#endif	/* !ZED_EVENT_H */
