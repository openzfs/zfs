// SPDX-License-Identifier: BSD-2-Clause
#ifndef	_OPENSOLARIS_SYS_SMP_H_
#define	_OPENSOLARIS_SYS_SMP_H_
#ifdef _KERNEL
#include <sys/cpu.h>
#ifndef mp_ncpus
#define	mp_ncpus	ncpu
#endif
#endif
#endif
