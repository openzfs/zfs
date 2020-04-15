/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 *
 * Copyright (C) 2013, 2020 Jorgen Lundman <lundman@lundman.net>
 *
 */

#include <sys/debug.h>
#include <sys/kmem.h>
#include <sys/systm.h>
#include <mach/mach_types.h>
#include <libkern/libkern.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <sys/taskq.h>
#include <kern/processor.h>
#include <sys/types.h>
#include <sys/sysctl.h>

#define	_task_user_
#include <IOKit/IOLib.h>

#include <sys/systeminfo.h>

static utsname_t utsname_static = { { 0 } };

unsigned int max_ncpus = 0;
uint64_t  total_memory = 0;
uint64_t  real_total_memory = 0;

// Size in bytes of the memory allocated in seg_kmem
extern uint64_t		segkmem_total_mem_allocated;

extern int bsd_hostname(char *, size_t, size_t *);
static char spl_hostname[MAXHOSTNAMELEN];

#ifdef __arm64__

/* Currently lua's setjmp does not work on arm64 */

void
longjmp(void *env, int val)
{
}

int
setjmp(void *env)
{
	return (0);
}
#endif

utsname_t *
utsname(void)
{
	return (&utsname_static);
}

/*
 * Solaris delay is in ticks (hz) and Darwin uses microsecs
 * 1 HZ is 10 milliseconds
 */
void
osx_delay(int ticks)
{
	if (ticks < 2) {
		// IODelay spins and takes microseconds as an argument
		// don't spend more than 10msec spinning.
		IODelay(ticks * 10000);
		return;
	}

	// ticks are 10 msec units
	int64_t ticks_to_go = (int64_t)ticks;
	// zfs_lbolt() is in 10 mec units
	int64_t start_tick = (int64_t)zfs_lbolt();
	int64_t end_tick = start_tick + (int64_t)ticks_to_go;

	do {
		IOSleep(ticks_to_go);
		int64_t cur_tick = (int64_t)zfs_lbolt();
		ticks_to_go = (end_tick - cur_tick);
	} while (ticks_to_go > 0);

}


uint32_t
zone_get_hostid(void *zone)
{
	size_t len;
	uint32_t myhostid = 0;

	len = sizeof (myhostid);
	sysctlbyname("kern.hostid", &myhostid, &len, NULL, 0);
	return (myhostid);
}

extern void *(*__ihook_malloc)(size_t size);
extern void (*__ihook_free)(void *);

const char *
spl_panicstr(void)
{
	return (NULL);
}

extern int get_system_inshutdown(void);

int
spl_system_inshutdown(void)
{
	// return (get_system_inshutdown());
	return (1);
}

#include <mach-o/loader.h>
typedef struct mach_header_64	kernel_mach_header_t;
#include <mach-o/nlist.h>
typedef struct nlist_64			kernel_nlist_t;

typedef struct segment_command_64 kernel_segment_command_t;

typedef struct _loaded_kext_summary {
	char		name[KMOD_MAX_NAME];
	uuid_t		uuid;
	uint64_t	address;
	uint64_t	size;
	uint64_t	version;
	uint32_t	loadTag;
	uint32_t	flags;
	uint64_t	reference_list;
} OSKextLoadedKextSummary;

typedef struct _loaded_kext_summary_header {
    uint32_t version;
    uint32_t entry_size;
    uint32_t numSummaries;
    uint32_t reserved; /* explicit alignment for gdb  */
    OSKextLoadedKextSummary summaries[0];
} OSKextLoadedKextSummaryHeader;

extern OSKextLoadedKextSummaryHeader *gLoadedKextSummaries;

typedef struct _cframe_t {
	struct _cframe_t	*prev;
	uintptr_t			caller;
#if PRINT_ARGS_FROM_STACK_FRAME
	unsigned			args[0];
#endif
} cframe_t;

extern kernel_mach_header_t _mh_execute_header;

extern kmod_info_t *kmod; /* the list of modules */

extern addr64_t  kvtophys(vm_offset_t va);

static int __maybe_unused
panic_print_macho_symbol_name(kernel_mach_header_t *mh, vm_address_t search,
    const char *module_name)
{
	kernel_nlist_t			*sym = NULL;
	struct load_command		*cmd;
	kernel_segment_command_t	*orig_ts = NULL, *orig_le = NULL;
	struct symtab_command		*orig_st = NULL;
	unsigned int			i;
	char				*strings, *bestsym = NULL;
	vm_address_t			bestaddr = 0, diff, curdiff;

	/*
	 * Assume that if it's loaded and linked into the kernel,
	 * it's a valid Mach-O
	 */
	cmd = (struct load_command *)&mh[1];
	for (i = 0; i < mh->ncmds; i++) {
		if (cmd->cmd == LC_SEGMENT_64) {
			kernel_segment_command_t *orig_sg =
			    (kernel_segment_command_t *)cmd;

			if (strncmp(SEG_TEXT, orig_sg->segname,
			    sizeof (orig_sg->segname)) == 0)
				orig_ts = orig_sg;
			else if (strncmp(SEG_LINKEDIT, orig_sg->segname,
			    sizeof (orig_sg->segname)) == 0)
				orig_le = orig_sg;
			/* pre-Lion i386 kexts have a single unnamed segment */
			else if (strncmp("", orig_sg->segname,
			    sizeof (orig_sg->segname)) == 0)
				orig_ts = orig_sg;
		} else if (cmd->cmd == LC_SYMTAB)
			orig_st = (struct symtab_command *)cmd;

		cmd = (struct load_command *)((uintptr_t)cmd + cmd->cmdsize);
	}

	if ((orig_ts == NULL) || (orig_st == NULL) || (orig_le == NULL))
		return (0);

	if ((search < orig_ts->vmaddr) ||
	    (search >= orig_ts->vmaddr + orig_ts->vmsize)) {
		/* search out of range for this mach header */
		return (0);
	}

	sym = (kernel_nlist_t *)(uintptr_t)(orig_le->vmaddr +
	    orig_st->symoff - orig_le->fileoff);
	strings = (char *)(uintptr_t)(orig_le->vmaddr +
	    orig_st->stroff - orig_le->fileoff);
	diff = search;

	for (i = 0; i < orig_st->nsyms; i++) {
		if (sym[i].n_type & N_STAB) continue;

		if (sym[i].n_value <= search) {
			curdiff = search - (vm_address_t)sym[i].n_value;
			if (curdiff < diff) {
				diff = curdiff;
				bestaddr = sym[i].n_value;
				bestsym = strings + sym[i].n_un.n_strx;
			}
		}
	}

	if (bestsym != NULL) {
		if (diff != 0) {
			printf("%s : %s + 0x%lx", module_name, bestsym,
			    (unsigned long)diff);
		} else {
			printf("%s : %s", module_name, bestsym);
		}
		return (1);
	}
	return (0);
}


static void __maybe_unused
panic_print_kmod_symbol_name(vm_address_t search)
{
#if 0	// gLoadedKextSummaries is no longer available
	uint_t i;
	if (gLoadedKextSummaries == NULL)
		return;
	for (i = 0; i < gLoadedKextSummaries->numSummaries; ++i) {
		OSKextLoadedKextSummary *summary =
		    gLoadedKextSummaries->summaries + i;

		if ((search >= summary->address) &&
		    (search < (summary->address + summary->size))) {
			kernel_mach_header_t *header =
			    (kernel_mach_header_t *)(uintptr_t)summary->address;
			if (panic_print_macho_symbol_name(header, search,
			    summary->name) == 0) {
				printf("%s + %llu", summary->name,
				    (unsigned long)search - summary->address);
			}
			break;
		}
	}
#endif
}


static void
panic_print_symbol_name(vm_address_t search)
{
	/* try searching in the kernel */
#if 0
	if (panic_print_macho_symbol_name(&_mh_execute_header,
	    search, "mach_kernel") == 0) {
		/* that failed, now try to search for the right kext */
		panic_print_kmod_symbol_name(search);
	}
#endif
}


void
spl_backtrace(char *thesignal)
{
	void *stackptr = NULL;

	printf("SPL: backtrace \"%s\"\n", thesignal);

#if defined(__i386__)
	__asm__ volatile("movl %%ebp, %0" : "=m" (stackptr));
#elif defined(__x86_64__)
	__asm__ volatile("movq %%rbp, %0" : "=m" (stackptr));
#endif

	int frame_index;
	int nframes = 16;
	cframe_t *frame = (cframe_t *)stackptr;

	for (frame_index = 0; frame_index < nframes; frame_index++) {
		vm_offset_t curframep = (vm_offset_t)frame;
		if (!curframep)
			break;
		if (curframep & 0x3) {
			printf("SPL: Unaligned frame\n");
			break;
		}
#if 0
		// no kvtophys() available now. Used to verify only?
		// pmap_find_phys(kernel_pmap, curframep) ?
		if (!kvtophys(curframep) ||
		    !kvtophys(curframep + sizeof (cframe_t) - 1)) {
			printf("SPL: No mapping exists for frame pointer\n");
			break;
		}
#endif
		printf("SPL: %p : 0x%lx ", frame, frame->caller);
		panic_print_symbol_name((vm_address_t)frame->caller);
		printf("\n");
		frame = frame->prev;
	}
}

int
getpcstack(uintptr_t *pcstack, int pcstack_limit)
{
	int  depth = 0;
	void *stackptr = NULL;

#if defined(__i386__)
	__asm__ volatile("movl %%ebp, %0" : "=m" (stackptr));
#elif defined(__x86_64__)
	__asm__ volatile("movq %%rbp, %0" : "=m" (stackptr));
#endif

	int frame_index;
	int nframes = pcstack_limit;
	cframe_t *frame = (cframe_t *)stackptr;

	for (frame_index = 0; frame_index < nframes; frame_index++) {
		vm_offset_t curframep = (vm_offset_t)frame;
		if (!curframep)
			break;
		if (curframep & 0x3) {
			break;
		}
#if 0
		if (!kvtophys(curframep) ||
		    !kvtophys(curframep + sizeof (cframe_t) - 1)) {
			break;
		}
#endif
		pcstack[depth++] = frame->caller;
		frame = frame->prev;
	}

	return (depth);
}

void
print_symbol(uintptr_t symbol)
{
	printf("SPL: ");
	panic_print_symbol_name((vm_address_t)(symbol));
	printf("\n");
}

int
ddi_copyin(const void *from, void *to, size_t len, int flags)
{
	int ret = 0;

	/* Fake ioctl() issued by kernel, 'from' is a kernel address */
	if (flags & FKIOCTL)
		memcpy(to, from, len);
	else
		ret = copyin((user_addr_t)from, (void *)to, len);

	return (ret);
}

int
ddi_copyout(const void *from, void *to, size_t len, int flags)
{
	int ret = 0;

	/* Fake ioctl() issued by kernel, 'from' is a kernel address */
	if (flags & FKIOCTL) {
		memcpy(to, from, len);
	} else {
		ret = copyout(from, (user_addr_t)to, len);
	}

	return (ret);
}

/*
 * Technically, this call does not exist in illumos, but we use it for
 * consistency.
 */
int
ddi_copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done)
{
	int ret;
	size_t local_done;

#undef copyinstr
	ret = copyinstr((user_addr_t)uaddr, kaddr, len, &local_done);
	if (done != NULL)
		*done = local_done;
	return (ret);
}

kern_return_t
spl_start(kmod_info_t *ki, void *d)
{
	printf("SPL: loading\n");

	int ncpus;
	size_t len = sizeof (ncpus);

	/*
	 * Boot load time is excessively early, so we have to wait
	 * until certain subsystems are available. Surely there is
	 * a more elegant way to do this wait?
	 */

	while (current_proc() == NULL) {
		printf("SPL: waiting for kernel init...\n");
		delay(hz>>1);
	}

	while (1) {
		len = sizeof (total_memory);
		sysctlbyname("hw.memsize", &total_memory, &len, NULL, 0);
		if (total_memory != 0) break;

		printf("SPL: waiting for sysctl...\n");
		delay(hz>>1);
	}

	sysctlbyname("hw.logicalcpu_max", &max_ncpus, &len, NULL, 0);
	if (!max_ncpus) max_ncpus = 1;

	/*
	 * Setting the total memory to physmem * 50% here, since kmem is
	 * not in charge of all memory and we need to leave some room for
	 * the OS X allocator. We internally add pressure if we step over it
	 */
	real_total_memory = total_memory;
	total_memory = total_memory * 50ULL / 100ULL;
	physmem = total_memory / PAGE_SIZE;

#if defined(__arm64__)
	/*
	 * 128GiB Studio Ultras with 12.6.1 and earlier will panic, usually in
	 * another kernel subsystem (hfs, hardware video encoding/decoding),
	 * after we allocate more than around 30GiB of memory through
	 * IOMallocAligned().
	 *
	 * So far this has not been observed on other hw platforms and has not
	 * been tested in 13.x (Ventura) on these systems.  However, few other
	 * macOS hw platforms have more than 64 GiB of RAM.  96 GiB of RAM is
	 * likely to be the lower limit for running into this problem, since
	 * smaller systems either [a] will not have total_memory >= 32 GiB,
	 * [b] will have kernel pressure signals driven by memory use in
	 * userland and the HFS/APFS buffer cache, or [c] both [a]and[b].
	 *
	 * For safety, on ARM we default to having a dynamic memory cap of 26
	 * GiB any ARM with more than 64GiB of RAM.  This will prevent ARC
	 * growth from climbing much above 20 GiB, consequently limiting the
	 * various other ZFS caches and overheads. Total consumption will not
	 * reach the panic-inviting levels around/above 32 GiB, since the ARC
	 * will be shrunk when approaching the dynamic memory cap.
	 *
	 * This has proven safe enough, and can be overridden dynamically by a
	 * sysctl or zsysctl.conf by setting
	 * kstat.spl.misc.spl_misc.spl_osif_dynamic_memory_cap to some other
	 * byte count, including 0 (which will allow growth until
	 * IOMallocAligned() returns a NULL).
	 */

	extern _Atomic uint64_t spl_dynamic_memory_cap;

	if (real_total_memory >= 64LL*1024LL*1024LL*1024LL)
		spl_dynamic_memory_cap = 26LL*1024LL*1024LL*1024LL;
#endif

	len = sizeof (utsname_static.sysname);
	sysctlbyname("kern.ostype", &utsname_static.sysname, &len, NULL, 0);

	/*
	 * For some reason, (CTLFLAG_KERN is not set) looking up hostname
	 * returns 1. So we set it to uuid just to give it *something*.
	 * As it happens, ZFS sets the nodename on init.
	 */
	len = sizeof (utsname_static.nodename);
	sysctlbyname("kern.uuid", &utsname_static.nodename, &len, NULL, 0);

	len = sizeof (utsname_static.release);
	sysctlbyname("kern.osrelease", &utsname_static.release, &len, NULL, 0);

	len = sizeof (utsname_static.version);
	sysctlbyname("kern.version", &utsname_static.version, &len, NULL, 0);

	strlcpy(spl_hostname, "noname", sizeof (spl_hostname));
	// Private.exports
	// bsd_hostname(spl_hostname, sizeof (spl_hostname), &len);

	strlcpy(utsname_static.nodename, spl_hostname,
	    sizeof (utsname_static.nodename));

	spl_mutex_subsystem_init();
	spl_kmem_init(total_memory);
	spl_vnode_init();
	spl_kmem_thread_init();
	spl_kmem_mp_init();

	return (KERN_SUCCESS);
}

kern_return_t
spl_stop(kmod_info_t *ki, void *d)
{
	spl_kmem_thread_fini();
	spl_vnode_fini();
	spl_taskq_fini();
	spl_rwlock_fini();
	spl_tsd_fini();
	spl_kmem_fini();
	spl_kstat_fini();
	spl_mutex_subsystem_fini();

	return (KERN_SUCCESS);
}
