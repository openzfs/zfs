// SPDX-License-Identifier: BSD-2-Clause
/*
 * DTrace / cyclic per-CPU ABI.  Defined and exported by
 * solaris.kmod (not openzfs).  Layout matches NetBSD
 * external/cddl/osnet/sys/sys/cpuvar.h so a stock
 * dtrace.kmod can resolve cpu_core[] / solaris_cpu[].
 *
 * LOCKDEBUG: omit cpu_core[] so openzfs still builds and
 * you still get the mutex debug.  dtrace.kmod will not
 * load (unresolved cpu_core).  solaris_cpu[] and
 * solaris_kmem_* stay.  Do not move the pid lock.
 */

#ifndef _OPENSOLARIS_SYS_CPUVAR_H_
#define	_OPENSOLARIS_SYS_CPUVAR_H_

#ifdef _KERNEL_OPT
#include "opt_lockdebug.h"
#endif

#include <sys/param.h>
#include <sys/types.h>
#include <sys/mutex.h>
#include <sys/time.h>

#ifdef _KERNEL

#ifndef MAXCPUS
#error "MAXCPUS is NetBSD's compile-time CPU cap; <sys/param.h> must define it"
#endif

#define	CPU_CACHE_COHERENCE_SIZE	64

struct cyc_cpu;

/*
 * Same field order as NetBSD osnet.  Do not pad: dtrace.kmod
 * indexes solaris_cpu[cpu_id] with the native stride.
 */
typedef struct {
	int		cpuid;
	struct cyc_cpu	*cpu_cyclic;
	uint32_t	cpu_flags;
	uint_t		cpu_intr_actv;
	uintptr_t	cpu_profile_pc;
	uintptr_t	cpu_profile_upc;
	uintptr_t	cpu_dtrace_caller;
	hrtime_t	cpu_dtrace_chillmark;
	hrtime_t	cpu_dtrace_chilled;
} solaris_cpu_t;

#define	cpu_t	solaris_cpu_t
#define	cpu_id	cpuid

#define	CPU_ENABLE	0x010

/*
 * NetBSD osnet embeds cpuc_pid_lock in this record and pads
 * to 64 bytes.  The pid provider takes
 * &cpu_core[cpu].cpuc_pid_lock at that offset -- do not move
 * the lock or change the stride.
 *
 * LOCKDEBUG inflates kmutex_t past 64 bytes.  Omit the type
 * and the array so the kmod still compiles; you keep the
 * LOCKDEBUG mutex checks.  dtrace.kmod fails to resolve
 * cpu_core and will not load.  That is expected.
 *
 * Gate on LOCKDEBUG itself -- sizeof is not a preprocessor
 * constant, so a #if on CPUC_SIZE would not see the growth.
 */
#ifdef LOCKDEBUG
#define	ZFS_HAVE_CPU_CORE	0
#else
#define	ZFS_HAVE_CPU_CORE	1
#define	CPUC_SIZE	(sizeof (uint16_t) + sizeof (uintptr_t) + \
			    sizeof (kmutex_t))
#define	CPUC_PADSIZE	(CPU_CACHE_COHERENCE_SIZE - CPUC_SIZE)

typedef struct cpu_core {
	uint16_t	cpuc_dtrace_flags;
	uint8_t		cpuc_pad[CPUC_PADSIZE];
	uintptr_t	cpuc_dtrace_illval;
	kmutex_t	cpuc_pid_lock;
} cpu_core_t;

extern cpu_core_t	cpu_core[MAXCPUS];
#endif /* LOCKDEBUG */

extern solaris_cpu_t	solaris_cpu[MAXCPUS];

#define	CPU_DTRACE_NOFAULT	0x0001
#define	CPU_DTRACE_DROP		0x0002
#define	CPU_DTRACE_BADADDR	0x0004
#define	CPU_DTRACE_BADALIGN	0x0008
#define	CPU_DTRACE_DIVZERO	0x0010
#define	CPU_DTRACE_ILLOP	0x0020
#define	CPU_DTRACE_NOSCRATCH	0x0040
#define	CPU_DTRACE_KPRIV	0x0080
#define	CPU_DTRACE_UPRIV	0x0100
#define	CPU_DTRACE_TUPOFLOW	0x0200
#if defined(__sparc__) || defined(__sparc)
#define	CPU_DTRACE_FAKERESTORE	0x0400
#endif
#define	CPU_DTRACE_ENTRY	0x0800
#define	CPU_DTRACE_BADSTACK	0x1000
#define	CPU_DTRACE_FAULT	(CPU_DTRACE_BADADDR | CPU_DTRACE_BADALIGN | \
	CPU_DTRACE_DIVZERO | CPU_DTRACE_ILLOP | CPU_DTRACE_NOSCRATCH | \
	CPU_DTRACE_KPRIV | CPU_DTRACE_UPRIV | CPU_DTRACE_TUPOFLOW | \
	CPU_DTRACE_BADSTACK)
#define	CPU_DTRACE_ERROR	(CPU_DTRACE_FAULT | CPU_DTRACE_DROP)

#endif /* _KERNEL */

#endif /* _OPENSOLARIS_SYS_CPUVAR_H_ */
