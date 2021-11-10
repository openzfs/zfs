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
