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

#ifndef	ZED_EXEC_H
#define	ZED_EXEC_H

#include <stdint.h>
#include "zed_strings.h"
#include "zed_conf.h"

void zed_exec_fini(void);

int zed_exec_process(uint64_t eid, const char *class, const char *subclass,
    struct zed_conf *zcp, zed_strings_t *envs);

#endif	/* !ZED_EXEC_H */
