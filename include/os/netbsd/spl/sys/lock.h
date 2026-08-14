// SPDX-License-Identifier: BSD-2-Clause
#ifndef	_OPENSOLARIS_SYS_LOCK_H_
#define	_OPENSOLARIS_SYS_LOCK_H_

#ifdef _KERNEL
#include_next <sys/lock.h>
#endif

#ifndef LK_RETRY
#define	LK_RETRY	0x00000200
#endif

#endif /* _OPENSOLARIS_SYS_LOCK_H_ */
