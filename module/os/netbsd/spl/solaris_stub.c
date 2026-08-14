// SPDX-License-Identifier: BSD-2-Clause
/*
 * solaris.kmod -- DTrace / cyclic / fbt ABI.
 *
 * Prebuilt dtrace.kmod depends on solaris, not openzfs.  It
 * unresolved-refs solaris_kmem_*, solaris_cpu and cpu_core.
 * Those symbols live here.  OpenZFS does not use them.
 *
 * LOCKDEBUG: kmutex_t no longer fits the 64-byte cpu_core
 * record.  cpuvar.h then leaves ZFS_HAVE_CPU_CORE at 0 and
 * we omit the array.  This kmod and openzfs still build;
 * you keep the LOCKDEBUG mutex checks.  dtrace.kmod will
 * not load (unresolved cpu_core).  That is expected.
 *
 * No MODULE depend on openzfs: a MAXCPUS mismatch fails
 * dtrace, not zfs.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/cpuvar.h>
#include <sys/mutex.h>
#include <sys/kmem.h>
#include <sys/errno.h>

#undef kmem_alloc
#undef kmem_zalloc
#undef kmem_free

int __sysctl_verify_ctype_arg(int, int);
int
__sysctl_verify_ctype_arg(int ctype, int arg)
{
	(void) ctype;
	(void) arg;
	return (0);
}

void *
solaris_kmem_alloc(size_t size, int kmflags)
{
	if (size == 0)
		return (NULL);
	return (kmem_alloc(size, kmflags));
}

void *
solaris_kmem_zalloc(size_t size, int kmflags)
{
	if (size == 0)
		return (NULL);
	return (kmem_zalloc(size, kmflags));
}

void
solaris_kmem_free(void *buf, size_t size)
{
	if (buf == NULL)
		return;
	kmem_free(buf, size);
}

#if ZFS_HAVE_CPU_CORE
cpu_core_t	cpu_core[MAXCPUS];
#endif
solaris_cpu_t	solaris_cpu[MAXCPUS];

#if ZFS_HAVE_CPU_CORE
typedef char cpu_core_size_ok[
    sizeof (cpu_core_t) == CPU_CACHE_COHERENCE_SIZE ? 1 : -1];
#endif
typedef char solaris_cpu_count_ok[
    sizeof (solaris_cpu) == sizeof (solaris_cpu_t) * MAXCPUS ? 1 : -1];

static void
solaris_cpu_init(void)
{
	int i;

	for (i = 0; i < MAXCPUS; i++) {
		solaris_cpu[i].cpuid = i;
		solaris_cpu[i].cpu_flags |= CPU_ENABLE;
#if ZFS_HAVE_CPU_CORE
		mutex_init(&cpu_core[i].cpuc_pid_lock, NULL,
		    MUTEX_DEFAULT, NULL);
#endif
	}
}

static void
solaris_cpu_fini(void)
{
#if ZFS_HAVE_CPU_CORE
	int i;

	for (i = 0; i < MAXCPUS; i++)
		mutex_destroy(&cpu_core[i].cpuc_pid_lock);
#endif
}

MODULE(MODULE_CLASS_MISC, solaris, NULL);

static int
solaris_modcmd(modcmd_t cmd, void *arg)
{
	(void) arg;
	switch (cmd) {
	case MODULE_CMD_INIT:
		/*
		 * ncpu is capped at MAXCPUS by the kernel.  This
		 * fires only if the kmod was built against different
		 * headers than the running kernel.  Fail here so
		 * dtrace cannot attach to a short array; openzfs
		 * is not involved.
		 */
		if (ncpu > MAXCPUS) {
			printf("solaris.kmod: ncpu=%d exceeds MAXCPUS=%d\n",
			    ncpu, MAXCPUS);
			return (E2BIG);
		}
		solaris_cpu_init();
		return (0);
	case MODULE_CMD_FINI:
		solaris_cpu_fini();
		return (0);
	default:
		return (ENOTTY);
	}
}
