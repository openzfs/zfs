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

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include "zed_log.h"

#define	ZED_LOG_MAX_ID_LEN	64
#define	ZED_LOG_MAX_LOG_LEN	1024

static struct {
	unsigned do_stderr:1;
	unsigned do_syslog:1;
	int level;
	char id[ZED_LOG_MAX_ID_LEN];
	int pipe_fd[2];
} _ctx;

void
zed_log_init(const char *identity)
{
	const char *p;

	if (identity) {
		p = (p = strrchr(identity, '/')) ? p + 1 : identity;
		strlcpy(_ctx.id, p, sizeof (_ctx.id));
	} else {
		_ctx.id[0] = '\0';
	}
	_ctx.pipe_fd[0] = -1;
	_ctx.pipe_fd[1] = -1;
}

void
zed_log_fini()
{
	if (_ctx.do_syslog) {
		closelog();
	}
}

/*
 * Create pipe for communicating daemonization status between the parent and
 *   child processes across the double-fork().
 */
void
zed_log_pipe_open(void)
{
	if ((_ctx.pipe_fd[0] != -1) || (_ctx.pipe_fd[1] != -1))
		zed_log_die("Invalid use of zed_log_pipe_open in PID %d",
		    (int) getpid());

	if (pipe(_ctx.pipe_fd) < 0)
		zed_log_die("Failed to create daemonize pipe in PID %d: %s",
		    (int) getpid(), strerror(errno));
}

/*
 * Close the read-half of the daemonize pipe.
 * This should be called by the child after fork()ing from the parent since
 *   the child will never read from this pipe.
 */
void
zed_log_pipe_close_reads(void)
{
	if (_ctx.pipe_fd[0] < 0)
		zed_log_die(
		    "Invalid use of zed_log_pipe_close_reads in PID %d",
		    (int) getpid());

	if (close(_ctx.pipe_fd[0]) < 0)
		zed_log_die(
		    "Failed to close reads on daemonize pipe in PID %d: %s",
		    (int) getpid(), strerror(errno));

	_ctx.pipe_fd[0] = -1;
}

/*
 * Close the write-half of the daemonize pipe.
 * This should be called by the parent after fork()ing its child since the
 *   parent will never write to this pipe.
 * This should also be called by the child once initialization is complete
 *   in order to signal the parent that it can safely exit.
 */
void
zed_log_pipe_close_writes(void)
{
	if (_ctx.pipe_fd[1] < 0)
		zed_log_die(
		    "Invalid use of zed_log_pipe_close_writes in PID %d",
		    (int) getpid());

	if (close(_ctx.pipe_fd[1]) < 0)
		zed_log_die(
		    "Failed to close writes on daemonize pipe in PID %d: %s",
		    (int) getpid(), strerror(errno));

	_ctx.pipe_fd[1] = -1;
}

/*
 * Block on reading from the daemonize pipe until signaled by the child
 *   (via zed_log_pipe_close_writes()) that initialization is complete.
 * This should only be called by the parent while waiting to exit after
 *   fork()ing the child.
 */
void
zed_log_pipe_wait(void)
{
	ssize_t n;
	char c;

	if (_ctx.pipe_fd[0] < 0)
		zed_log_die("Invalid use of zed_log_pipe_wait in PID %d",
		    (int) getpid());

	for (;;) {
		n = read(_ctx.pipe_fd[0], &c, sizeof (c));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			zed_log_die(
			    "Failed to read from daemonize pipe in PID %d: %s",
			    (int) getpid(), strerror(errno));
		}
		if (n == 0) {
			break;
		}
	}
}

void
zed_log_stderr_open(int level)
{
	_ctx.do_stderr = 1;
	_ctx.level = level;
}

void
zed_log_stderr_close(void)
{
	_ctx.do_stderr = 0;
}

void
zed_log_syslog_open(int facility)
{
	const char *identity;

	_ctx.do_syslog = 1;
	identity = (_ctx.id[0] == '\0') ? NULL : _ctx.id;
	openlog(identity, LOG_NDELAY, facility);
}

void
zed_log_syslog_close(void)
{
	_ctx.do_syslog = 0;
	closelog();
}

static void
_zed_log_aux(int priority, const char *fmt, va_list vargs)
{
	char buf[ZED_LOG_MAX_LOG_LEN];
	char *syslogp;
	char *p;
	int len;
	int n;

	assert(fmt != NULL);

	syslogp = NULL;
	p = buf;
	len = sizeof (buf);

	if (_ctx.id[0] != '\0') {
		n = snprintf(p, len, "%s: ", _ctx.id);
		if ((n < 0) || (n >= len)) {
			p += len - 1;
			len = 0;
		} else {
			p += n;
			len -= n;
		}
	}
	if ((len > 0) && fmt) {
		syslogp = p;
		n = vsnprintf(p, len, fmt, vargs);
		if ((n < 0) || (n >= len)) {
			p += len - 1;
			len = 0;
		} else {
			p += n;
			len -= n;
		}
	}
	*p = '\0';

	if (_ctx.do_syslog && syslogp)
		syslog(priority, "%s", syslogp);

	if (_ctx.do_stderr && priority <= _ctx.level)
		fprintf(stderr, "%s\n", buf);
}

/*
 * Log a message at the given [priority] level specified by the printf-style
 *   format string [fmt].
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
