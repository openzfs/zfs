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

#ifndef	ZED_LOG_H
#define	ZED_LOG_H

#include <syslog.h>

void zed_log_init(const char *identity);

void zed_log_fini(void);

void zed_log_pipe_open(void);

void zed_log_pipe_close_reads(void);

void zed_log_pipe_close_writes(void);

void zed_log_pipe_wait(void);

void zed_log_stderr_open(int priority);

void zed_log_stderr_close(void);

void zed_log_syslog_open(int facility);

void zed_log_syslog_close(void);

void zed_log_msg(int priority, const char *fmt, ...);

void zed_log_die(const char *fmt, ...);

#endif	/* !ZED_LOG_H */
