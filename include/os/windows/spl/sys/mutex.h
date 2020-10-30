/*
* CDDL HEADER START
*
* The contents of this file are subject to the terms of the
* Common Development and Distribution License (the "License").
* You may not use this file except in compliance with the License.
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
*
* Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
*
*/

#ifndef OSX_MUTEX_H
#define OSX_MUTEX_H

#include <spl_config.h> // For SPL_DEBUG_MUTEX

#ifdef _KERNEL
//#include <libkern/locks.h>
//#include <wdm.h>
//#include <ntddk.h>
#include <intsafe.h>
#include <ntifs.h>

//#include <libkern/OSAtomic.h>
//#include <kern/locks.h>
//#include <kern/thread.h>
#include <sys/proc.h>


typedef enum {
    MUTEX_ADAPTIVE = 0,     /* spin if owner is running, otherwise block */
    MUTEX_SPIN = 1,         /* block interrupts and spin */
    MUTEX_DRIVER = 4,       /* driver (DDI) mutex */
    MUTEX_DEFAULT = 6       /* kernel default mutex */
} kmutex_type_t;

typedef struct {
	KEVENT opaque;
} mutex_t;

/*
 * Solaris kmutex defined.
 *
 * and is embedded into ZFS structures (see dbuf) so we need to match the
 * size carefully. It appears to be 32 bytes. Or rather, it needs to be
 * aligned.
 */

typedef struct kmutex {
	mutex_t m_lock;
	void           *m_owner;
	unsigned int initialised;
	unsigned int set_event_guard;
} kmutex_t;


#define MUTEX_HELD(x)           (mutex_owned(x))
#define MUTEX_NOT_HELD(x)       (!mutex_owned(x))

#define mutex_init spl_mutex_init
void spl_mutex_init(kmutex_t *mp, char *name, kmutex_type_t type, void *ibc);

#define mutex_enter spl_mutex_enter
void spl_mutex_enter(kmutex_t *mp);

#define mutex_enter_nested(A, B)        mutex_enter(A)
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
struct kthread *spl_mutex_owner(kmutex_t *mp);

int  spl_mutex_subsystem_init(void);
void spl_mutex_subsystem_fini(void);

#endif  // KERNEL
#endif
