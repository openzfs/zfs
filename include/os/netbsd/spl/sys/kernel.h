// SPDX-License-Identifier: BSD-2-Clause
#ifndef	_OPENSOLARIS_SYS_KERNEL_H_
#define	_OPENSOLARIS_SYS_KERNEL_H_

#include_next <sys/kernel.h>

#ifdef _KERNEL

#ifndef MTX_DEF
#define	MTX_DEF		0
#endif

#define	MTX_SYSINIT(name, mtx, desc, opts)	/* inited in module load */
#define	SYSINIT(...)
#define	SYSUNINIT(...)

#ifndef hz
/* native param.h exposes hz only for _KERNEL; keep a fallback */
extern int hz;
#endif

#endif /* _KERNEL */

#endif /* _OPENSOLARIS_SYS_KERNEL_H_ */
