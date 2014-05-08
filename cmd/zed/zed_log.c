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
}

void
zed_log_fini()
{
	if (_ctx.do_syslog) {
		closelog();
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
