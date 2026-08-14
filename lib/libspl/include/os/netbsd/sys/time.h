// SPDX-License-Identifier: CDDL-1.0
/*
 * Prefer the native header.  lib/libspl/include/sys/time.h
 * include_next's this way so <time.h> does not see clock_gettime
 * before NetBSD's __RENAME(__clock_gettime50).
 */
#ifndef	_LIBSPL_OS_NETBSD_SYS_TIME_H_
#define	_LIBSPL_OS_NETBSD_SYS_TIME_H_

#include_next <sys/time.h>

#endif /* _LIBSPL_OS_NETBSD_SYS_TIME_H_ */
