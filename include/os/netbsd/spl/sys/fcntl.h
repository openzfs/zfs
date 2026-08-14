// SPDX-License-Identifier: CDDL-1.0
#ifndef	_OPENSOLARIS_SYS_FCNTL_H_
#define	_OPENSOLARIS_SYS_FCNTL_H_
#include_next <sys/fcntl.h>
#define	FIGNORECASE	0x80000
#ifndef FOFFMAX
#define	FOFFMAX		0
#endif
#ifndef O_LARGEFILE
#define	O_LARGEFILE	0
#endif
#ifndef F_FREESP
#define	F_FREESP	11
#endif
#ifndef flock64_t
typedef struct flock	flock64_t;
#endif
#endif
