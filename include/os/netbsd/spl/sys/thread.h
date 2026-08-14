// SPDX-License-Identifier: BSD-2-Clause
#ifndef _SPL_THREAD_H_
#define	_SPL_THREAD_H_

#ifdef _KERNEL
#include <sys/proc.h>

#define	getcomm()	((curlwp->l_name != NULL &&			\
			    curlwp->l_name[0] != '\0') ?		\
			    curlwp->l_name : curlwp->l_proc->p_comm)
#define	getpid()	(curlwp->l_proc->p_pid)

/*
 * Bytes left on this LWP's kernel stack, measured from the first
 * mapped page (x86 has an unmapped hole after the PCB).  0 if
 * unknown.
 */
intptr_t spl_kstack_remaining(void);
#endif

#endif
