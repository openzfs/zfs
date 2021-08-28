#ifndef	_ZFS_PMEM_H_
#define	_ZFS_PMEM_H_

#include <sys/zfs_context.h>
#include <sys/simd.h>

/*
 * NOTE: when adding ops, update zfs_pmem_ops_init's validation code.
 */
typedef struct zfs_pmem_ops {
	const char *zpmem_op_name;
	boolean_t (*zpmem_op_check_supported)(void);
	void (*zpmem_op_memcpy256_nt_nodrain)(void *d, const void *s, size_t n, zfs_kfpu_ctx_t *kfpu_ctx);
	void (*zpmem_op_memzero256_nt_nodrain)(void *d, size_t n, zfs_kfpu_ctx_t *kfpu_ctx);
	void (*zpmem_op_drain)(void);
	int (*zpmem_op_init)(void);
	int (*zpmem_op_fini)(void);
	boolean_t zpmem_op_supported; /* set by pmem_ops_init */
	boolean_t zpmem_op_initialized; /* set by pmem_ops_init */
} zfs_pmem_ops_t;

/* must be called before any other call to the zfs_pmem_* functions */
int zfs_pmem_ops_init(void);
void zfs_pmem_ops_fini(void);

const zfs_pmem_ops_t * zfs_pmem_ops_get_current(void);
const char *zfs_pmem_ops_name(const zfs_pmem_ops_t *);
const zfs_pmem_ops_t* zfs_pmem_ops_get_by_name(const char *val);
/* ops must have been returned by zfs_pmem_ops_get_by_name */
void zfs_pmem_ops_set(const zfs_pmem_ops_t *ops);

void zfs_pmem_memcpy256_nt_nodrain(void *dst, const void *src, size_t n, zfs_kfpu_ctx_t *kfpu_ctx);
void zfs_pmem_memzero256_nt_nodrain(void *dst, size_t s, zfs_kfpu_ctx_t *kfpu_ctx);
void zfs_pmem_drain(void);

int zfs_pmem_memcpy_mcsafe(void *dst, const void *pmem_src, size_t n);

#endif	/* _ZFS_PMEM_H_ */
