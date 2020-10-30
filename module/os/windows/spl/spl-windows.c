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
 * Copyright (C) 2018 Jorgen Lundman <lundman@lundman.net>
 *
 */

#include <spl-debug.h>
#include <sys/kmem.h>

#include <sys/systm.h>
//#include <mach/mach_types.h>
//#include <libkern/libkern.h>

#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/utsname.h>
//#include <sys/ioctl.h>
#include <sys/taskq.h>
#include <sys/systeminfo.h>
#include <sys/sunddi.h>

//#define MACH_KERNEL_PRIVATE

//#include <kern/processor.h>

#define DEBUG 1  // for backtrace debugging info

static utsname_t utsname_static = { { 0 } };

//extern struct machine_info      machine_info;

unsigned int max_ncpus = 0;
uint64_t  total_memory = 0;
uint64_t  real_total_memory = 0;

volatile unsigned int vm_page_free_wanted = 0;
volatile unsigned int vm_page_free_min = 512;
volatile unsigned int vm_page_free_count = 5000;
volatile unsigned int vm_page_speculative_count = 5500;

uint64_t spl_GetPhysMem(void);

#include <sys/types.h>
//#include <sys/sysctl.h>
/* protect against:
 * /System/Library/Frameworks/Kernel.framework/Headers/mach/task.h:197: error: conflicting types for ‘spl_thread_create’
 * ../../include/sys/thread.h:72: error: previous declaration of ‘spl_thread_create’ was here
 */
#define	_task_user_
//#include <IOKit/IOLib.h>

#include <Trace.h>

// Size in bytes of the memory allocated in seg_kmem
extern uint64_t		segkmem_total_mem_allocated;
#define MAXHOSTNAMELEN 64
extern char hostname[MAXHOSTNAMELEN];

uint32_t spl_hostid = 0;

#if defined(__clang__)
/*
 *  Try to figure out why we fail linking with these two missing
 * Appears to come from including intrin.h - except we don't.
 */
uint64_t __readcr8(void)
{
	return 0ULL;
}

unsigned long _byteswap_ulong(unsigned long b)
{
	return __builtin_bswap32(b);
}
#endif


utsname_t *
utsname(void)
{
        return (&utsname_static);
}

/*
 * Solaris delay is in ticks (hz) and Windows in 100 nanosecs
 * 1 HZ is 10 milliseconds, 10000000 nanoseconds.
 */
void
windows_delay(int ticks)
{
	LARGE_INTEGER interval;
	// * 10000000 / 100
	interval.QuadPart = -((uint64_t)ticks) * 100000ULL;
	KeDelayExecutionThread(KernelMode, FALSE, &interval);
}

uint32_t zone_get_hostid(void *zone)
{
	return spl_hostid;
}

const char *spl_panicstr(void)
{
    return "";
}

int spl_system_inshutdown(void)
{
    return 0;
}

void
hrt2ts(hrtime_t hrt, timespec_t *tsp)
{
	tsp->tv_sec = (time_t)(hrt / NANOSEC);
	tsp->tv_nsec = (hrt % NANOSEC);
}

// If we want to implement this on Windows, we could probably use
// https://stackoverflow.com/questions/590160/how-to-log-stack-frames-with-windows-x64
// which calls RtlCaptureStackBackTrace();
int
getpcstack(uintptr_t *pcstack, int pcstack_limit)
{
	return RtlCaptureStackBackTrace(1, pcstack_limit, (PVOID *)pcstack, NULL);
}

/*
 * fnv_32a_str - perform a 32 bit Fowler/Noll/Vo FNV-1a hash on a string
 *
 * input:
 *	str	- string to hash
 *	hval	- previous hash value or 0 if first call
 *
 * returns:
 *	32 bit hash as a static hash type
 *
 * NOTE: To use the recommended 32 bit FNV-1a hash, use FNV1_32A_INIT as the
 *  	 hval arg on the first call to either fnv_32a_buf() or fnv_32a_str().
 */
uint32_t
fnv_32a_str(const char *str, uint32_t hval)
{
	unsigned char *s = (unsigned char *)str;	/* unsigned string */

	/*
	 * FNV-1a hash each octet in the buffer
	 */
	while (*s) {

		/* xor the bottom with the current octet */
		hval ^= (uint32_t)*s++;

		/* multiply by the 32 bit FNV magic prime mod 2^32 */
#if defined(NO_FNV_GCC_OPTIMIZATION)
		hval *= FNV_32_PRIME;
#else
		hval += (hval << 1) + (hval << 4) + (hval << 7) + (hval << 8) + (hval << 24);
#endif
	}

	/* return our new hash value */
	return hval;
}

/*
 * fnv_32a_buf - perform a 32 bit Fowler/Noll/Vo FNV-1a hash on a buffer
 *
 * input:
 *buf- start of buffer to hash
 *len- length of buffer in octets
 *hval- previous hash value or 0 if first call
 *
 * returns:
 *32 bit hash as a static hash type
 *
 * NOTE: To use the recommended 32 bit FNV-1a hash, use FNV1_32A_INIT as the
 *  hval arg on the first call to either fnv_32a_buf() or fnv_32a_str().
 */
uint32_t
fnv_32a_buf(void *buf, size_t len, uint32_t hval)
{
	unsigned char *bp = (unsigned char *)buf;/* start of buffer */
	unsigned char *be = bp + len;/* beyond end of buffer */

	/*
	 * FNV-1a hash each octet in the buffer
	 */
	while (bp < be) {

		/* xor the bottom with the current octet */
		hval ^= (uint32_t)*bp++;

		/* multiply by the 32 bit FNV magic prime mod 2^32 */
#if defined(NO_FNV_GCC_OPTIMIZATION)
		hval *= FNV_32_PRIME;
#else
		hval += (hval << 1) + (hval << 4) + (hval << 7) + (hval << 8) + (hval << 24);
#endif
	}

	/* return our new hash value */
	return hval;
}

/*
 * Function to free a MDL chain
 */
void UnlockAndFreeMdl(PMDL Mdl)
{
	PMDL currentMdl, nextMdl;

	for (currentMdl = Mdl; currentMdl != NULL; currentMdl = nextMdl)
	{
		nextMdl = currentMdl->Next;
		if (currentMdl->MdlFlags & MDL_PAGES_LOCKED)
		{
			MmUnlockPages(currentMdl);
		}
		IoFreeMdl(currentMdl);
	}
}

int
ddi_copyin(const void *from, void *to, size_t len, int flags)
{
	int error = 0;
	PMDL  mdl = NULL;
	PCHAR buffer = NULL;

	if (from == NULL ||
		to == NULL ||
		len == 0)
		return 0;

    /* Fake ioctl() issued by kernel, so we just need to bcopy */
	if (flags & FKIOCTL) {
		if (flags & FCOPYSTR) 
			strlcpy(to, from, len);
		else
			bcopy(from, to, len);
		return 0;
	}

	//ret = copyin((user_addr_t)from, (void *)to, len);
	// Lets try reading from the input nvlist
	dprintf("SPL: trying windows copyin: %p:%d\n", from, len);

	try {
		ProbeForRead((void *)from, len, sizeof(UCHAR));
	}
	except(EXCEPTION_EXECUTE_HANDLER)
	{
		error = GetExceptionCode();
	}
	if (error) {
		TraceEvent(TRACE_ERROR, "SPL: Exception while accessing inBuf 0X%08X\n", error);
		goto out;
	}

	mdl = IoAllocateMdl((void *)from, len, FALSE, FALSE, NULL);
	if (!mdl) {
		error = STATUS_INSUFFICIENT_RESOURCES;
		goto out;
	}

	try {
		MmProbeAndLockPages(mdl, UserMode, IoReadAccess);
	}
	except(EXCEPTION_EXECUTE_HANDLER)
	{
		error = GetExceptionCode();
	}
	if (error) {
		TraceEvent(TRACE_ERROR, "SPL: Exception while locking inBuf 0X%08X\n", error);
		goto out;
	}

	buffer = MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority | MdlMappingNoExecute);

	if (!buffer) {
		error = STATUS_INSUFFICIENT_RESOURCES;
	} else {
		// Success, copy over the data.
		if (flags & FCOPYSTR)
			strlcpy(to, buffer, len);
		else
			bcopy(buffer, to, len);
	}

	dprintf("SPL: copyin return %d (%d bytes)\n", error, len);

out:
	if (mdl) {
		UnlockAndFreeMdl(mdl);
	}

	return error;
}


int
ddi_copyout(const void *from, void *to, size_t len, int flags)
{
	int error = 0;
	PMDL  mdl = NULL;
	PCHAR buffer = NULL;

	if (from == NULL ||
		to == NULL ||
		len == 0)
		return 0;

	/* Fake ioctl() issued by kernel, 'from' is a kernel address */
	if (flags & FKIOCTL) {
		bcopy(from, to, len);
		return 0;
	}

	//dprintf("SPL: trying windows copyout: %p:%d\n", to, len);

	mdl = IoAllocateMdl(to, len, FALSE, FALSE, NULL);
	if (!mdl) {
		error = STATUS_INSUFFICIENT_RESOURCES;
		TraceEvent(TRACE_ERROR, "SPL: copyout failed to allocate mdl\n");
		goto out;
	}

	try {
		MmProbeAndLockPages(mdl, UserMode, IoWriteAccess);
	}
	except(EXCEPTION_EXECUTE_HANDLER)
	{
		error = GetExceptionCode();
	}
	if (error != 0) {
		TraceEvent(TRACE_ERROR, "SPL: Exception while locking outBuf 0X%08X\n",
			error);
		goto out;
	}

	buffer = MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority | MdlMappingNoExecute);

	if (!buffer) {
		error = STATUS_INSUFFICIENT_RESOURCES;
		goto out;
	} else {
		// Success, copy over the data.
		bcopy(from, buffer, len);
	}
	//dprintf("SPL: copyout return %d (%d bytes)\n", error, len);
out:
	if (mdl) {
		UnlockAndFreeMdl(mdl);
	}

	return error;
}

int
ddi_copysetup(void *to, size_t len, void **out_buffer, PMDL *out_mdl)
{
	int error = 0;
	PMDL  mdl = NULL;
	PCHAR buffer = NULL;

	if (to == NULL ||
		out_buffer == NULL ||
		out_mdl == NULL ||
		len == 0)
		return 0;

	//dprintf("SPL: trying windows copyout_ex: %p:%d\n", to, len);

	// Do we have to call both? Or is calling ProbeForWrite enough?
	try {
		ProbeForRead(to, len, sizeof(UCHAR));
	}
	except(EXCEPTION_EXECUTE_HANDLER)
	{
		error = GetExceptionCode();
	}
	if (error) {
		TraceEvent(TRACE_ERROR, "SPL: Exception while accessing inBuf 0X%08X\n", error);
		goto out;
	}

	try {
		ProbeForWrite(to, len, sizeof(UCHAR));
	}
	except(EXCEPTION_EXECUTE_HANDLER)
	{
		error = GetExceptionCode();
	}
	if (error) {
		TraceEvent(TRACE_ERROR, "SPL: Exception while accessing inBuf 0X%08X\n", error);
		goto out;
	}

	mdl = IoAllocateMdl(to, len, FALSE, FALSE, NULL);
	if (!mdl) {
		error = STATUS_INSUFFICIENT_RESOURCES;
		TraceEvent(TRACE_ERROR, "SPL: copyout failed to allocate mdl\n");
		goto out;
	}

	try {
		MmProbeAndLockPages(mdl, UserMode, IoWriteAccess);
	}
	except(EXCEPTION_EXECUTE_HANDLER)
	{
		error = GetExceptionCode();
	}
	if (error != 0) {
		TraceEvent(TRACE_ERROR, "SPL: Exception while locking outBuf 0X%08X\n",
			error);
		goto out;
	}

	buffer = MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority | MdlMappingNoExecute);

	if (!buffer) {
		error = STATUS_INSUFFICIENT_RESOURCES;
		goto out;
	}

	*out_buffer = buffer;
	*out_mdl = mdl;
	return 0;

out:
	if (mdl) {
		UnlockAndFreeMdl(mdl);
	}

	return error;
}


/* Technically, this call does not exist in IllumOS, but we use it for
 * consistency.
 */
int ddi_copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done)
{
	int ret = 0;

	ret = ddi_copyin((const void *)uaddr, kaddr, len, FCOPYSTR);
	if ((ret == STATUS_SUCCESS) && done) {
		*done = strlen(kaddr) + 1; // copyinstr includes the NULL byte
	}
	return ret;
}




int spl_start (void)
{
    //max_ncpus = processor_avail_count;
    // int ncpus;
    // size_t len = sizeof(ncpus);

	dprintf("SPL: start\n");
    max_ncpus = KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS);
	if (!max_ncpus) max_ncpus = 1;
	dprintf("SPL: total ncpu %d\n", max_ncpus);

	// Not sure how to get physical RAM size in a Windows Driver
	// So until then, pull some numbers out of the aether. Next
	// we could let users pass in a value, somehow...
	total_memory = spl_GetPhysMem();

	// Set 2GB as code above doesnt work
	if (!total_memory)
		total_memory = 2ULL * 1024ULL * 1024ULL * 1024ULL;

	dprintf("SPL: memsize %llu (before adjustment)\n", total_memory);
	/*
	 * Setting the total memory to physmem * 80% here, since kmem is
	 * not in charge of all memory and we need to leave some room for
	 * the OS X allocator. We internally add pressure if we step over it
	 */
    real_total_memory = total_memory;
    total_memory = total_memory * 50ULL / 100ULL; // smd: experiment with 50%, 8GiB
    physmem = total_memory / PAGE_SIZE;

	// We need to set these to some non-zero values
	// so we don't think there is permanent memory
	// pressure.
	vm_page_free_count = (unsigned int)(physmem/2ULL);
	vm_page_speculative_count = vm_page_free_count;

    /*
     * For some reason, (CTLFLAG_KERN is not set) looking up hostname
     * returns 1. So we set it to uuid just to give it *something*.
     * As it happens, ZFS sets the nodename on init.
     */
    //len = sizeof(utsname.nodename);
    //sysctlbyname("kern.uuid", &utsname.nodename, &len, NULL, 0);

    //len = sizeof(utsname.release);
    //sysctlbyname("kern.osrelease", &utsname.release, &len, NULL, 0);

    //len = sizeof(utsname.version);
    //sysctlbyname("kern.version", &utsname.version, &len, NULL, 0);

    //strlcpy(utsname.nodename, hostname, sizeof(utsname.nodename));
    strlcpy(utsname_static.nodename, "Windows", sizeof(utsname_static.nodename));
    spl_mutex_subsystem_init();
	//DbgBreakPoint();
	spl_kmem_init(total_memory);

	spl_tsd_init();
	spl_rwlock_init();
	spl_taskq_init();

    spl_vnode_init();
	spl_kmem_thread_init();
	spl_kmem_mp_init();

    IOLog("SPL: Loaded module v%s-%s%s, "
          "(ncpu %d, memsize %llu, pages %llu)\n",
          SPL_META_VERSION, SPL_META_RELEASE, SPL_DEBUG_STR,
		  max_ncpus, total_memory, physmem);
	return STATUS_SUCCESS;
}

extern uint64_t zfs_threads;

int spl_stop (void)
{
	spl_kmem_thread_fini();
    spl_vnode_fini();
    spl_taskq_fini();
    spl_rwlock_fini();
	spl_tsd_fini();
    spl_kmem_fini();
	// XXX: we run into a bunch of problems with this kstat_fini stuff, as it calls vmem_fini a second time
	// after spl_kmem_fini()->kernelheap_fini()->vmem_fini(heap_arena) got called
	// and therefore destroys global structures twice
	// so skip that for the moment
	//
	// spl_kstat_fini();	
    spl_mutex_subsystem_fini();
    IOLog("SPL: Unloaded module v%s-%s "
          "(os_mem_alloc: %llu)\n",
          SPL_META_VERSION, SPL_META_RELEASE,
		  segkmem_total_mem_allocated);
	while (zfs_threads >= 1) {
		IOLog("SPL: active threads %d\n", zfs_threads);
		delay(hz << 2);
	}
	return STATUS_SUCCESS;
}




#define UNICODE

#pragma pack(push, 4)
typedef struct {
	UCHAR  Type;
	UCHAR  ShareDisposition;
	USHORT Flags;
	ULONGLONG Start;
	ULONG Length;
} MEMORY, *PMEMORY;
#pragma pack(pop)

/* TimoVJL */
LONGLONG GetMemResources(char *pData)
{
	LONGLONG llMem = 0;
	char *pPtr;
	uint32_t *pDW;
	pDW = (uint32_t *)pData;
	if (*pDW != 1) return 0;
	DWORD nCnt = *(uint32_t *)(pData + 0x10);	// Count
	pPtr = pData + 0x14;
	DWORD nRLen = 0;
	if (*(pData + 0x14) == *(pData + 0x24)) nRLen = 16;
	if (*(pData + 0x14) == *(pData + 0x28)) nRLen = 20;
	PMEMORY pMem;
	for (DWORD nIdx = 0; nRLen && nIdx < nCnt; nIdx++) {
		pMem = (PMEMORY)(pPtr + nRLen * nIdx);
		if (pMem->Type == 3) llMem += pMem->Length;
		if (pMem->Type == 7 && pMem->Flags == 0x200) llMem += ((LONGLONG)pMem->Length) << 8;
		pMem += nRLen;
	}
	return llMem;
}

NTSTATUS
spl_query_memsize(
	IN PWSTR ValueName,
	IN ULONG ValueType,
	IN PVOID ValueData,
	IN ULONG ValueLength,
	IN PVOID Context,
	IN PVOID EntryContext
)
{

	dprintf("%s: '%S' type 0x%x len 0x%x\n", __func__,
		ValueName, ValueType, ValueLength);

	if ((ValueType == REG_RESOURCE_LIST) &&
		(_wcsicmp(L".Translated", ValueName) == 0)) {
		uint64_t *value;
		value = EntryContext;
		if (value)
			*value = GetMemResources(ValueData);
		dprintf("%s: memsize is %llu\n", __func__, value ? *value : 0);
	}

	return STATUS_SUCCESS;
}


uint64_t spl_GetPhysMem(void)
{
	uint64_t memory;
	NTSTATUS status;
	static RTL_QUERY_REGISTRY_TABLE query[2] = 
	{ 
		{
		.Flags = RTL_QUERY_REGISTRY_REQUIRED
				/*| RTL_QUERY_REGISTRY_DIRECT*/
				| RTL_QUERY_REGISTRY_NOEXPAND
				| RTL_QUERY_REGISTRY_TYPECHECK,
		.QueryRoutine = spl_query_memsize,
		} 
	};

	query[0].EntryContext = &memory;
	status = RtlQueryRegistryValues(
		RTL_REGISTRY_ABSOLUTE,
		L"\\REGISTRY\\MACHINE\\HARDWARE\\RESOURCEMAP\\System Resources\\Physical Memory",
		query, NULL, NULL);

	if (status != STATUS_SUCCESS) {
		TraceEvent(TRACE_ERROR, "%s: size query failed: 0x%x\n", __func__, status);
		return 0ULL;
	}

	return memory;
}


