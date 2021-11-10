/*
 * This file is part of the ZFS Event Daemon (ZED).
 *
 * Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049).
 * Copyright (C) 2013-2014 Lawrence Livermore National Security, LLC.
 * Refer to the OpenZFS git commit log for authoritative copyright attribution.
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License Version 1.0 (CDDL-1.0).
 * You can obtain a copy of the license from the top-level file
 * "OPENSOLARIS.LICENSE" or at <http://opensource.org/licenses/CDDL-1.0>.
 * You may not use this file except in compliance with the license.
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
