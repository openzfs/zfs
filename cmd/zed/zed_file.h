/*
 * This file is part of the ZFS Event Daemon (ZED).
 *
 * Developed at Lawrence Livermore National Laboratory (LLNL-CODE-403049).
 * Copyright (C) 2013-2014 Lawrence Livermore National Security, LLC.
 * Refer to the ZoL git commit log for authoritative copyright attribution.
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License Version 1.0 (CDDL-1.0).
 * You can obtain a copy of the license from the top-level file
 * "OPENSOLARIS.LICENSE" or at <http://opensource.org/licenses/CDDL-1.0>.
 * You may not use this file except in compliance with the license.
 */

#ifndef	ZED_FILE_H
#define	ZED_FILE_H

#include <sys/types.h>
#include <unistd.h>

ssize_t zed_file_read_n(int fd, void *buf, size_t n);

ssize_t zed_file_write_n(int fd, void *buf, size_t n);

int zed_file_lock(int fd);

int zed_file_unlock(int fd);

pid_t zed_file_is_locked(int fd);

void zed_file_close_from(int fd);

int zed_file_close_on_exec(int fd);

#endif	/* !ZED_FILE_H */
