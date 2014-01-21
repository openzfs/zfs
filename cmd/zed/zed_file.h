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

int zed_file_create_dirs(const char *dir_name);

#endif	/* !ZED_FILE_H */
