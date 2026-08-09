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

#ifndef	ZED_FILE_H
#define	ZED_FILE_H

#include <sys/types.h>
#include <unistd.h>

int zed_file_lock(int fd);

int zed_file_unlock(int fd);

pid_t zed_file_is_locked(int fd);

void zed_file_close_from(int fd);

#endif	/* !ZED_FILE_H */
