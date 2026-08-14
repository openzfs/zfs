// SPDX-License-Identifier: BSD-2-Clause
#ifndef	_OPENSOLARIS_SYS_MALLOC_H_
#define	_OPENSOLARIS_SYS_MALLOC_H_

#ifdef _KERNEL
#include_next <sys/malloc.h>
#define	M_SOLARIS	M_DEVBUF
#endif

#endif /* _OPENSOLARIS_SYS_MALLOC_H_ */
