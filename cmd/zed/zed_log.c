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

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>
#include "zed_log.h"

#define	ZED_LOG_MAX_LOG_LEN	1024

static struct {
	unsigned do_stderr:1;
	unsigned do_syslog:1;
	const char *identity;
	int priority;
	int pipe_fd[2];
} _ctx;

/*
 * Initialize the logging subsystem.
 */
void
zed_log_init(const char *identity)
{
	if (identity) {
		const char *p = strrchr(identity, '/');
		_ctx.identity = (p != NULL) ? p + 1 : identity;
	} else {
		_ctx.identity = NULL;
	}
	_ctx.pipe_fd[0] = -1;
	_ctx.pipe_fd[1] = -1;
}

/*
 * Shutdown the logging subsystem.
 */
void
zed_log_fini(void)
{
	zed_log_stderr_close();
	zed_log_syslog_close();
}

/*
 * Create pipe for communicating daemonization status between the parent and
 * child processes across the double-fork().
 */
void
zed_log_pipe_open(void)
{
	if ((_ctx.pipe_fd[0] != -1) || (_ctx.pipe_fd[1] != -1))
		zed_log_die("Invalid use of zed_log_pipe_open in PID %d",
		    (int)getpid());

	if (pipe(_ctx.pipe_fd) < 0)
		zed_log_die("Failed to create daemonize pipe in PID %d: %s",
		    (int)getpid(), strerror(errno));
}

/*
 * Close the read-half of the daemonize pipe.
 *
 * This should be called by the child after fork()ing from the parent since
 * the child will never read from this pipe.
 */
void
zed_log_pipe_close_reads(void)
{
	if (_ctx.pipe_fd[0] < 0)
		zed_log_die(
		    "Invalid use of zed_log_pipe_close_reads in PID %d",
		    (int)getpid());

	if (close(_ctx.pipe_fd[0]) < 0)
		zed_log_die(
		    "Failed to close reads on daemonize pipe in PID %d: %s",
		    (int)getpid(), strerror(errno));

	_ctx.pipe_fd[0] = -1;
}

/*
 * Close the write-half of the daemonize pipe.
 *
 * This should be called by the parent after fork()ing its child since the
 * parent will never write to this pipe.
 *
 * This should also be called by the child once initialization is complete
 * in order to signal the parent that it can safely exit.
 */
void
zed_log_pipe_close_writes(void)
{
	if (_ctx.pipe_fd[1] < 0)
		zed_log_die(
		    "Invalid use of zed_log_pipe_close_writes in PID %d",
		    (int)getpid());

	if (close(_ctx.pipe_fd[1]) < 0)
		zed_log_die(
		    "Failed to close writes on daemonize pipe in PID %d: %s",
		    (int)getpid(), strerror(errno));

	_ctx.pipe_fd[1] = -1;
}

/*
 * Block on reading from the daemonize pipe until signaled by the child
 * (via zed_log_pipe_close_writes()) that initialization is complete.
 *
 * This should only be called by the parent while waiting to exit after
 * fork()ing the child.
 */
void
zed_log_pipe_wait(void)
{
	ssize_t n;
	char c;

	if (_ctx.pipe_fd[0] < 0)
		zed_log_die("Invalid use of zed_log_pipe_wait in PID %d",
		    (int)getpid());

	for (;;) {
		n = read(_ctx.pipe_fd[0], &c, sizeof (c));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			zed_log_die(
			    "Failed to read from daemonize pipe in PID %d: %s",
			    (int)getpid(), strerror(errno));
		}
		if (n == 0) {
			break;
		}
	}
}

/*
 * Start logging messages at the syslog [priority] level or higher to stderr.
 * Refer to syslog(3) for valid priority values.
 */
void
zed_log_stderr_open(int priority)
{
	_ctx.do_stderr = 1;
	_ctx.priority = priority;
}

/*
 * Stop logging messages to stderr.
 */
void
zed_log_stderr_close(void)
{
	if (_ctx.do_stderr)
		_ctx.do_stderr = 0;
}

/*
 * Start logging messages to syslog.
 * Refer to syslog(3) for valid option/facility values.
 */
void
zed_log_syslog_open(int facility)
{
	_ctx.do_syslog = 1;
	openlog(_ctx.identity, LOG_NDELAY | LOG_PID, facility);
}

/*
 * Stop logging messages to syslog.
 */
void
zed_log_syslog_close(void)
{
	if (_ctx.do_syslog) {
		_ctx.do_syslog = 0;
		closelog();
	}
}

/*
 * Auxiliary function to log a message to syslog and/or stderr.
 */
static void
_zed_log_aux(int priority, const char *fmt, va_list vargs)
{
	char buf[ZED_LOG_MAX_LOG_LEN];
	int n;

	if (!fmt)
		return;

	n = vsnprintf(buf, sizeof (buf), fmt, vargs);
	if ((n < 0) || (n >= sizeof (buf))) {
		buf[sizeof (buf) - 2] = '+';
		buf[sizeof (buf) - 1] = '\0';
	}

	if (_ctx.do_syslog)
		syslog(priority, "%s", buf);

	if (_ctx.do_stderr && (priority <= _ctx.priority))
		fprintf(stderr, "%s\n", buf);
}

/*
 * Log a message at the given [priority] level specified by the printf-style
 * format string [fmt].
 */
void
zed_log_msg(int priority, const char *fmt, ...)
{
	va_list vargs;

	if (fmt) {
		va_start(vargs, fmt);
		_zed_log_aux(priority, fmt, vargs);
		va_end(vargs);
	}
}

/*
 * Log a fatal error message specified by the printf-style format string [fmt].
 */
void
zed_log_die(const char *fmt, ...)
{
	va_list vargs;

	if (fmt) {
		va_start(vargs, fmt);
		_zed_log_aux(LOG_ERR, fmt, vargs);
		va_end(vargs);
	}
	exit(EXIT_FAILURE);
}
