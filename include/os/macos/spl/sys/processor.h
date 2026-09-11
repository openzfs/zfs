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
 *
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef	_SPL_PROCESSOR_H
#define	_SPL_PROCESSOR_H

#include <sys/types.h>

extern uint32_t getcpuid(void);

#if defined(__amd64__) || defined(__i386__)

#include <i386/cpuid.h>

#define	__cpuid_count(__level, __count, __eax, __ebx, __ecx, __edx) \
	__asm("cpuid" : "=a"(__eax), "=b" (__ebx), "=c"(__ecx), "=d"(__edx) \
		: "0"(__level), "2"(__count))

static inline unsigned int
__get_cpuid_max(unsigned int __ext, unsigned int *__sig)
{
	uint32_t r[4];

	do_cpuid(__ext, r);
	if (__sig)
		*__sig = r[ebx];
	return (r[eax]);
}

/* macOS does have do_cpuid() macro */
static inline int
__get_cpuid(unsigned int __level,
    unsigned int *__eax, unsigned int *__ebx,
    unsigned int *__ecx, unsigned int *__edx)
{
	unsigned int __ext = __level & 0x80000000;
	uint32_t r[4];

	if (__get_cpuid_max(__ext, 0) < __level)
		return (0);
	do_cpuid(__ext, r);
	*__eax = r[eax];
	*__ebx = r[ebx];
	*__ecx = r[ecx];
	*__edx = r[edx];
	return (1);
}

#endif // x86

typedef int	processorid_t;
extern int spl_processor_init(void);

#endif /* _SPL_PROCESSOR_H */
