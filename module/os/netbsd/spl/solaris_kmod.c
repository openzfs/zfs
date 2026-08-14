// SPDX-License-Identifier: BSD-2-Clause
/*
 * OpenZFS-side SPL init.  DTrace ABI (solaris_cpu, cpu_core,
 * exported solaris_kmem_*) lives in solaris.kmod, not here.
 * LOCKDEBUG omits cpu_core; dtrace will not load.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>

int
solaris__init(void)
{
	extern void opensolaris_utsname_init(void);
	extern void spl_atomic64_init(void);
	extern void spl_cred_init(void);
	extern void spl_taskq_init(void);
	extern void spl_kstat_init(void);
	extern void spl_kmem_init(void);
	extern int spl_uvm_pin_init(void);
	extern void callb_init(void *);
	int error;

	opensolaris_utsname_init();
	callb_init(NULL);
	spl_atomic64_init();
	spl_cred_init();
	spl_taskq_init();
	spl_kstat_init();
	spl_kmem_init();
	if ((error = spl_uvm_pin_init()) != 0) {
		extern void spl_kstat_fini(void);
		extern void spl_kmem_fini(void);
		extern void spl_atomic64_fini(void);
		extern void spl_taskq_fini(void);
		extern void spl_cred_fini(void);
		extern void callb_fini(void *);

		spl_kstat_fini();
		spl_taskq_fini();
		spl_cred_fini();
		spl_atomic64_fini();
		callb_fini(NULL);
		spl_kmem_fini();
		return (error);
	}
	return (0);
}

int
solaris__fini(void)
{
	extern void spl_kstat_fini(void);
	extern void spl_kmem_fini(void);
	extern void spl_uvm_pin_fini(void);
	extern void spl_atomic64_fini(void);
	extern void spl_taskq_fini(void);
	extern void spl_cred_fini(void);
	extern void callb_fini(void *);

	spl_uvm_pin_fini();
	spl_kstat_fini();
	spl_taskq_fini();
	spl_cred_fini();
	spl_atomic64_fini();
	callb_fini(NULL);
	spl_kmem_fini();
	return (0);
}
