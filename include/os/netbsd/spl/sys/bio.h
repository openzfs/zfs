// SPDX-License-Identifier: BSD-2-Clause
#ifndef	_OPENSOLARIS_SYS_BIO_H_
#define	_OPENSOLARIS_SYS_BIO_H_
#ifdef _KERNEL
#include <sys/buf.h>
struct bio;
#define	BIO_READ	B_READ
#define	BIO_WRITE	B_WRITE
#endif
#endif
