// SPDX-License-Identifier: BSD-2-Clause
/*
 * NetBSD misc SPL: utsname, copyin/out, thread_create.
 *
 * thread_create / thread_exit map onto kthread_create / kthread_exit,
 * same as Linux/FreeBSD.  Earlier setjmp/longjmp parking was a
 * misdiagnosis of amap_wipeout: that panic came from walking zpool(8)'s
 * vmspace (vn_rdwr on a user LWP).  UIO_SYSSPACE uses
 * kernel vmspace; the hop is gone.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/kmem.h>
#include <sys/vnode.h>
#include <sys/misc.h>
#include <sys/file.h>
#include <sys/debug.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/kthread.h>
#include <sys/cred.h>
#include <sys/kauth.h>
#include <sys/lwp.h>
#include <machine/pcb.h>
#include <uvm/uvm_extern.h>
#include <uvm/uvm_pmap.h>

cred_t *kcred;

void
spl_cred_init(void)
{
	/*
	 * proc0's cred is the kernel cred. Hold it for the life of
	 * the module so spa/vdev I/O that passes kcred is not the
	 * calling process, and crfree(kcred) cannot drop that process.
	 * kauth_cred_hold is void on NetBSD -- it takes a ref in place.
	 */
	kauth_cred_hold(proc0.p_cred);
	kcred = (cred_t *)proc0.p_cred;
}

void
spl_cred_fini(void)
{
	if (kcred != NULL) {
		kauth_cred_free((kauth_cred_t)kcred);
		kcred = NULL;
	}
}

/*
 * Make vn_open / VOP_ACCESS see cr instead of curlwp->l_cred.
 * Needed when vdev_open_children runs on a taskq worker: curlwp
 * is then a kernel thread, but the ioctl credential must still
 * be the one that opened the pool (device_access ZTS).
 * Returns the previous l_cred, or NULL if no swap was needed.
 */
kauth_cred_t
zfs_lwp_cred_swap(cred_t *cr)
{
	kauth_cred_t old;

	if (cr == NULL)
		return (NULL);
	old = curlwp->l_cred;
	if (old == (kauth_cred_t)cr)
		return (NULL);
	kauth_cred_hold((kauth_cred_t)cr);
	curlwp->l_cred = (kauth_cred_t)cr;
	return (old);
}

void
zfs_lwp_cred_restore(kauth_cred_t old, cred_t *cr)
{
	if (old == NULL)
		return;
	curlwp->l_cred = old;
	kauth_cred_free((kauth_cred_t)cr);
}

static struct opensolaris_utsname hw_utsname;

utsname_t *
utsname(void)
{
	return (&hw_utsname);
}

void
opensolaris_utsname_init(void)
{
	strlcpy(hw_utsname.sysname, "NetBSD", sizeof (hw_utsname.sysname));
	strlcpy(hw_utsname.nodename, hostname, sizeof (hw_utsname.nodename));
	strlcpy(hw_utsname.release, osrelease, sizeof (hw_utsname.release));
	strlcpy(hw_utsname.machine, MACHINE, sizeof (hw_utsname.machine));
	(void) snprintf(hw_utsname.version, sizeof (hw_utsname.version),
	    "%s", osrelease);
}

char *
kmem_strdup(const char *s)
{
	char *buf;
	size_t n = strlen(s) + 1;

	buf = kmem_alloc(n, KM_SLEEP);
	memcpy(buf, s, n);
	return (buf);
}

int
ddi_copyin(const void *from, void *to, size_t len, int flags)
{
	if (flags & FKIOCTL) {
		memcpy(to, from, len);
		return (0);
	}
	return (copyin(from, to, len));
}

int
ddi_copyout(const void *from, void *to, size_t len, int flags)
{
	if (flags & FKIOCTL) {
		memcpy(to, from, len);
		return (0);
	}
	return (copyout(from, to, len));
}

struct zfs_nb_thr {
	void		(*t_func)(void *);
	void		*t_arg;
};

static void
zfs_nb_thr_wrapper(void *arg)
{
	struct zfs_nb_thr *t = arg;
	void (*fn)(void *) = t->t_func;
	void *farg = t->t_arg;

	kmem_free(t, sizeof (*t));
	(*fn)(farg);
	kthread_exit(0);
}

kthread_t *
solaris__thread_create(void *stk, size_t stksize, void (*proc)(void *),
    void *arg, size_t len, proc_t *pp, int state, pri_t pri, const char *name)
{
	struct zfs_nb_thr *t;
	lwp_t *lwp = NULL;
	int error;

	(void) stksize;
	(void) pp;
	(void) len;
	ASSERT(stk == NULL && len == 0);
	ASSERT(state == TS_RUN);

	t = kmem_zalloc(sizeof (*t), KM_SLEEP);
	t->t_func = proc;
	t->t_arg = arg;
	error = kthread_create(pri, KTHREAD_MPSAFE, NULL,
	    zfs_nb_thr_wrapper, t, &lwp, "%s",
	    name != NULL ? name : "zfs_thr");
	if (error != 0) {
		printf("ZFS: kthread_create %s failed %d\n",
		    name != NULL ? name : "zfs_thr", error);
		kmem_free(t, sizeof (*t));
		return (NULL);
	}
	return (lwp);
}

void
thread_exit(void)
{
	kthread_exit(0);
}

int
vn_is_readonly(vnode_t *vp)
{
	return (vp->v_mount != NULL && (vp->v_mount->mnt_flag & MNT_RDONLY));
}

int
current_is_reclaim_thread(void)
{
	return (0);
}

/*
 * First mapped page at or after `from`, below `hi`.  NULL if none.
 */
static char *
spl_stack_first_mapped(char *from, char *hi)
{
	vaddr_t va, end;
	paddr_t pa;

	va = ((vaddr_t)from + PAGE_SIZE - 1) & ~(vaddr_t)(PAGE_SIZE - 1);
	end = (vaddr_t)hi;
	while (va + PAGE_SIZE <= end) {
		if (pmap_extract(pmap_kernel(), va, &pa))
			return ((char *)va);
		va += PAGE_SIZE;
	}
	return (NULL);
}

/*
 * Kernel stacks grow down.  l_addr is the u-area with the PCB at
 * the front.  KSTACK_LOWEST_ADDR is the MI hook for the first
 * usable stack byte, excluding a red zone.
 *
 * When sizeof(pcb) < PAGE_SIZE that address still sits in the
 * PCB page.  A port may then leave an unmapped guard before the
 * stack (x86: [PCB][optional FPU pages][hole][stack][trailing
 * hole]).  The CPU does not fault on a stack-pointer update
 * into the hole, only on a later load/store, so a recursive C
 * call double-faults (luaD_call, zcp table conversion, ...).
 *
 * Walk from the first page after the PCB.  A hole with mapped
 * pages after it is a guard: stack starts on the first mapped
 * page past it.  A hole only at the top of the u-area is a
 * trailing guard and is ignored.
 */

intptr_t
spl_kstack_remaining(void)
{
	char local;
	char *sp = &local;
	char *lo;
	char *hi;
	char *mapped;
	vaddr_t va;
	paddr_t pa;
	intptr_t rem;

	if (curlwp == NULL || curlwp->l_addr == NULL)
		return (0);
	lo = KSTACK_LOWEST_ADDR(curlwp);
	if (lo == NULL)
		return (0);
	hi = (char *)curlwp->l_addr + USPACE;
	va = ((vaddr_t)lo + PAGE_SIZE - 1) & ~(vaddr_t)(PAGE_SIZE - 1);
	while (va + PAGE_SIZE <= (vaddr_t)hi) {
		if (!pmap_extract(pmap_kernel(), va, &pa)) {
			mapped = spl_stack_first_mapped((char *)va + PAGE_SIZE,
			    hi);
			if (mapped != NULL)
				lo = mapped;
			break;
		}
		va += PAGE_SIZE;
	}
	rem = (intptr_t)(sp - lo);
	if (rem > 0 && rem < (intptr_t)USPACE)
		return (rem);
	return (0);
}
