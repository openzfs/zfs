#include <sys/zfs_pmem.h>

#ifdef __KERNEL__

#include <sys/pmem_spl.h>

static void
pmem_linuxkernel_drain_impl(void)
{
	__asm("sfence");
}

static void
pmem_linuxkernel_memcpy256_nt_nodrain(void *dst, const void *buf, size_t size)
{
	spl_memcpy_flushcache(dst, buf, size);
}

static uint8_t pmem_linuxkernel_256zeroes[256] = {0};

static void
pmem_linuxkernel_memzero256_nt_nodrain(void *dst, size_t size)
{
	CTASSERT(sizeof(pmem_linuxkernel_256zeroes) == 256);
	/* use VERIFY so that the compiler optimizes divisions to shifts */
	VERIFY0(P2PHASE_TYPED(size,
	    sizeof(pmem_linuxkernel_256zeroes), size_t));

	size_t iters = size /
	    sizeof(pmem_linuxkernel_256zeroes);

	for (size_t i = 0; i < iters; i++) {
		spl_memcpy_flushcache(dst, pmem_linuxkernel_256zeroes,
		    sizeof(pmem_linuxkernel_256zeroes));
	}
}

static boolean_t
pmem_linuxkernel_check_supported(void)
{
	return (B_TRUE);
}


zfs_pmem_ops_t pmem_ops_linuxkernel = {
	.zpmem_op_name = "linuxkernel",
	.zpmem_op_check_supported = pmem_linuxkernel_check_supported,
	.zpmem_op_memcpy256_nt_nodrain = pmem_linuxkernel_memcpy256_nt_nodrain,
	.zpmem_op_memzero256_nt_nodrain = pmem_linuxkernel_memzero256_nt_nodrain,
	.zpmem_op_drain = pmem_linuxkernel_drain_impl
};

#endif /* __KERNEL__ */
