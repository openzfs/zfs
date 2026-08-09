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

__attribute__((format(printf, 1, 2), __noreturn__))
void zed_log_die(const char *fmt, ...);

#endif	/* !ZED_LOG_H */
