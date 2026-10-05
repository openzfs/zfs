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

#ifndef _SPL_SYSLOG_H
#define	_SPL_SYSLOG_H

#include <wosix.h>

/*
 *  Facility codes
 */
#define	LOG_KERN	(0<<3)  /* kernel messages */
#define	LOG_USER	(1<<3)  /* random user-level messages */
#define	LOG_MAIL	(2<<3)  /* mail system */
#define	LOG_DAEMON	(3<<3)  /* system daemons */
#define	LOG_AUTH	(4<<3)  /* security/authorization messages */
#define	LOG_SYSLOG	(5<<3)  /* messages generated internally by syslogd */
#define	LOG_LPR		(6<<3)  /* line printer subsystem */
#define	LOG_NEWS	(7<<3)  /* netnews subsystem */
#define	LOG_UUCP	(8<<3)  /* uucp subsystem */
#define	LOG_ALTCRON	(9<<3)  /* BSD cron/at subsystem */
#define	LOG_AUTHPRIV	(10<<3) /* BSD security/authorization messages */
#define	LOG_FTP		(11<<3) /* file transfer subsystem */
#define	LOG_NTP		(12<<3) /* network time subsystem */
#define	LOG_AUDIT	(13<<3) /* audit subsystem */
#define	LOG_CONSOLE	(14<<3) /* BSD console messages */
#define	LOG_CRON	(15<<3) /* cron/at subsystem */
#define	LOG_LOCAL0	(16<<3) /* reserved for local use */
#define	LOG_LOCAL1	(17<<3) /* reserved for local use */
#define	LOG_LOCAL2	(18<<3) /* reserved for local use */
#define	LOG_LOCAL3	(19<<3) /* reserved for local use */
#define	LOG_LOCAL4	(20<<3) /* reserved for local use */
#define	LOG_LOCAL5	(21<<3) /* reserved for local use */
#define	LOG_LOCAL6	(22<<3) /* reserved for local use */
#define	LOG_LOCAL7	(23<<3) /* reserved for local use */

/*
 *  Priorities (these are ordered)
 */
#define	LOG_EMERG	0 /* system is unusable */
#define	LOG_ALERT	1 /* action must be taken immediately */
#define	LOG_CRIT	2 /* critical conditions */
#define	LOG_ERR		3 /* error conditions */
#define	LOG_WARNING	4 /* warning conditions */
#define	LOG_NOTICE	5 /* normal but signification condition */
#define	LOG_INFO	6 /* informational */
#define	LOG_DEBUG	7 /* debug-level messages */

#define	LOG_PID		0x01 /* log the pid with each message */
// #define LOG_CONS	0x02 /* log on the console if errors in sending */
// #define LOG_ODELAY	0x04 /* delay open until syslog() is called */
#define	LOG_NDELAY	0x08 /* don't delay open */
// #define LOG_NOWAIT	0x10 /* if forking to log on console, don't wait() */

void openlog(const char *, int, int);
// void syslog(int, const char *, ...);
// void closelog(void);
// int setlogmask(int);

#endif
