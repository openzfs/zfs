/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LIBSPL_WINDOWS_UNISTD_H
#define	_LIBSPL_WINDOWS_UNISTD_H

#include <sys/types.h>
#include <sys/types32.h>
#define	issetugid() (geteuid() == 0 || getegid() == 0)

#include <sys/stat.h>

#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <getopt.h>

extern int	opterr;
extern int	optind;
extern int	optopt;
extern int	optreset;
extern char	*optarg;

#include <stdarg.h>
#include <io.h>
#include <direct.h>

#define	_SC_OPEN_MAX		5
#define	_SC_PAGESIZE		11
#define	_SC_PAGE_SIZE		_SC_PAGESIZE
#define	_SC_NPROCESSORS_ONLN	15
#define	_SC_PHYS_PAGES		500
#define	_SC_IOV_MAX		600

#define	X_OK	1

#ifdef  __cplusplus
extern "C" {
#endif

extern uint64_t sysconf(int name);

extern size_t strlcpy(char *s, const char *t, size_t n);

extern size_t strlcat(char *s, const char *t, size_t n);

extern ssize_t getline_impl(char **linep, size_t *linecapp, FILE *stream,
    boolean_t internal);
extern ssize_t getline(char **linep, size_t *linecapp, FILE *stream);

// int pread_win(HANDLE h, void *buf, size_t nbyte, off_t offset);
extern int pipe(int fildes[2]);
extern char *realpath(const char *file_name, char *resolved_name);
extern int usleep(__int64 usec);
extern int vasprintf(char **strp, const char *fmt, va_list ap);
extern int asprintf(char **strp, const char *fmt, ...);
extern int strncasecmp(const char *s1, const char *s2, size_t n);
extern int readlink(const char *path, char *buf, size_t bufsize);
extern const char *getexecname(void);
#define	getprogname getexecname
extern uid_t getuid(void);
extern uid_t geteuid(void);
extern pid_t getpid(void);

struct zfs_cmd;
extern int mkstemp(char *tmpl);
extern int64_t gethrtime(void);
#if !defined(_WINSOCKAPI_) && !defined(_WINSOCK2API_)
#include <winsock2.h>
#pragma comment(lib, "Ws2_32.lib")
#endif
struct timezone;
extern int gettimeofday(struct timeval *tp, struct timezone *tzp);
extern void flockfile(FILE *file);
extern void funlockfile(FILE *file);
extern unsigned long gethostid(void);
extern char *strndup(const char *src, size_t size);
extern int setrlimit(int resource, const struct rlimit *rlp);

struct group *getgrgid(uint64_t gid);
struct passwd *getpwuid(uint64_t uid);
extern void syslog(int priority, const char *message, ...);
extern void closelog(void);

extern int unmount(const char *dir, int flags);

extern pid_t setsid(void);

static inline pid_t fork(void)
{
	return (0); // Return as child.
}

extern int mkostemps(char *templ, int suffixlen, DWORD flags);
void *reallocarray(void *optr, size_t nmemb, size_t size);
extern unsigned int alarm(unsigned int seconds);

#ifdef  __cplusplus
}
#endif

#endif /* _LIBSPL_WINDOWS_UNISTD_H */
