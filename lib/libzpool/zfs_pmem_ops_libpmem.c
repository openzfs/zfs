#include <sys/zfs_pmem.h>

#ifndef __KERNEL__

/* XXX include libpmem header */
// #include <libpmem.h>
void pmem_memset_nodrain(void *, int, size_t);
void pmem_memcpy_nodrain(void *, const void *, size_t);
void pmem_drain(void);
void pmem_memcpy_persist(void *, const void *, size_t);

static void
pmem_libpmem_memcpy256_nt_nodrain(void *dst, const void *src, size_t size)
{
	pmem_memcpy_nodrain(dst, src, size);
}

static void
pmem_libpmem_memzero256_nt_nodrain(void *dst, size_t size)
{
	pmem_memset_nodrain(dst, 0, size);
}

static boolean_t
pmem_libpmem_check_supported(void)
{
	return (B_TRUE);
}

zfs_pmem_ops_t pmem_ops_libpmem = {
    .zpmem_op_name = "libpmem",
    .zpmem_op_check_supported = pmem_libpmem_check_supported,
    .zpmem_op_memcpy256_nt_nodrain = pmem_libpmem_memcpy256_nt_nodrain,
    .zpmem_op_memzero256_nt_nodrain = pmem_libpmem_memzero256_nt_nodrain,
    .zpmem_op_drain = pmem_drain
};

#endif /* ! __KERNEL */
