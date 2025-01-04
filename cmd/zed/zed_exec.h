// SPDX-License-Identifier: CDDL-1.0
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

#ifndef	ZED_EXEC_H
#define	ZED_EXEC_H

#include <stdint.h>
#include "zed_strings.h"
#include "zed_conf.h"

void zed_exec_fini(void);

int zed_exec_process(uint64_t eid, const char *class, const char *subclass,
    struct zed_conf *zcp, zed_strings_t *envs);

#endif	/* !ZED_EXEC_H */
