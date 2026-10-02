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
 * Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef WINDOWS_MUTEX_H
#define	WINDOWS_MUTEX_H

struct _KTHREAD;
typedef struct _KTHREAD kthread_t;

#include <spl_config.h> // For SPL_DEBUG_MUTEX

#ifdef _KERNEL

#if __clang__
// We set this in top CMakelists.txt but these are emitted by the
// pre-processor, so we need to push them here as well.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunknown-pragmas"
#pragma GCC diagnostic ignored "-Wignored-attributes"
#pragma GCC diagnostic ignored "-Wignored-pragma-intrinsic"
#endif

#include <intsafe.h>
#include <ntifs.h>

#if __clang__
#pragma GCC diagnostic pop
#endif

#include <sys/proc.h>


typedef enum {
	MUTEX_ADAPTIVE = 0,	/* spin if owner is running, otherwise block */
	MUTEX_SPIN = 1,		/* block interrupts and spin */
	MUTEX_DRIVER = 4,	/* driver (DDI) mutex */
	MUTEX_DEFAULT = 6	/* kernel default mutex */
} kmutex_type_t;

/* arm64 NEEDs this aligned */
typedef struct {
	__declspec(align(8)) KEVENT opaque;
} mutex_t; // __attribute__((aligned(8)));

/*
 * Solaris kmutex defined.
 *
 * and is embedded into ZFS structures (see dbuf) so we need to match the
 * size carefully. It appears to be 32 bytes. Or rather, it needs to be
 * aligned.
 */

typedef struct kmutex {
	mutex_t		m_lock;
	void		*m_owner;
	/*
	 * If this struct is changed, also change kernel_mutex_t
	 */
	KSPIN_LOCK	m_destroy_lock;
	unsigned int	m_initialised;
	/*
	 * Fast-path optimization: m_waiters is incremented by threads that
	 * failed the CAS in mutex_enter and are about to sleep on m_lock.
	 * mutex_exit only acquires m_destroy_lock and calls KeSetEvent when
	 * this is non-zero, making uncontested exit a near-free operation.
	 */
	volatile uint32_t m_waiters;
} kmutex_t;

#define	MUTEX_HELD(x)		(mutex_owned(x))
#define	MUTEX_NOT_HELD(x)	(!mutex_owned(x))

#define	mutex_init spl_mutex_init
void spl_mutex_init(kmutex_t *mp, char *name, kmutex_type_t type, void *ibc);

#define	mutex_enter spl_mutex_enter
void spl_mutex_enter(kmutex_t *mp);

/* Until we can investigate interruptible on Windows */
static inline int mutex_enter_interruptible(kmutex_t *mp)
{
	spl_mutex_enter(mp);
	return (0);
}

#define	mutex_enter_nested(A, B)	mutex_enter(A)
#define	MUTEX_NOLOCKDEP	0

#define	mutex_destroy spl_mutex_destroy
#define	mutex_exit spl_mutex_exit
#define	mutex_tryenter spl_mutex_tryenter
#define	mutex_owned spl_mutex_owned
#define	mutex_owner spl_mutex_owner

void spl_mutex_destroy(kmutex_t *mp);
void spl_mutex_exit(kmutex_t *mp);
int  spl_mutex_tryenter(kmutex_t *mp);
int  spl_mutex_owned(kmutex_t *mp);
kthread_t *spl_mutex_owner(kmutex_t *mp);

int  spl_mutex_subsystem_init(void);
void spl_mutex_subsystem_fini(void);

#endif  // KERNEL
#endif
