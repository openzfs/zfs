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

/*
 * Real POSIX execlp() replaces the calling process image and never
 * returns on success. Windows has no equivalent syscall, so this
 * forwards to the CRT's _execvp() (itself spawn+wait+exit(), faking
 * exec semantics), except for "man": no such program exists on a
 * stock Windows install, so that specific target is intercepted and
 * handled directly -- see execlp() in posix.c.
 */
extern int execlp(const char *file, const char *arg0, ...);

extern int mkostemps(char *templ, int suffixlen, DWORD flags);
void *reallocarray(void *optr, size_t nmemb, size_t size);
extern unsigned int alarm(unsigned int seconds);

#ifdef  __cplusplus
}
#endif

#endif /* _LIBSPL_WINDOWS_UNISTD_H */
