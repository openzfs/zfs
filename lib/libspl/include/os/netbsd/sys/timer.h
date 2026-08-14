// SPDX-License-Identifier: CDDL-1.0
#ifndef	_LIBSPL_OS_NETBSD_SYS_TIMER_H
#define	_LIBSPL_OS_NETBSD_SYS_TIMER_H

/*
 * Cannot #define hz: NetBSD <sys/sysctl.h> has a struct field `int hz`,
 * and userspace <sys/param.h> only exports `extern int hz` under
 * _KERNEL.  An enumeration constant occupies the ordinary identifier
 * namespace without rewriting later struct members.
 */
#define	_LIBSPL_HZ_DEFINED
enum { hz = 119 };

#include_next <sys/timer.h>

#endif
