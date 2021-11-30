#include <sys/zfs_pmem.h>

#ifndef __KERNEL__
extern zfs_pmem_ops_t pmem_ops_libpmem;
#endif
extern zfs_pmem_ops_t pmem_ops_avx512;
#ifdef __KERNEL__
extern zfs_pmem_ops_t pmem_ops_linuxkernel;
#endif

static zfs_pmem_ops_t *zfs_pmem_op_impls[] = {
#ifndef __KERNEL__
	&pmem_ops_libpmem,
#endif
	&pmem_ops_avx512,
#ifdef __KERNEL__
	&pmem_ops_linuxkernel,
#endif
	NULL,
};

#define	ITER_PMEM_OP_IMPLS_FWD(i, op) \
	for (i = 0, op = zfs_pmem_op_impls[i]; \
	    op != NULL; \
	    i += 1, op = zfs_pmem_op_impls[i])

int
zfs_pmem_ops_init(void)
{
	int ret = 0;
	size_t i, j;
	zfs_pmem_ops_t *op, *opi, *opj;

	if (ZFS_DEBUG) {
		ITER_PMEM_OP_IMPLS_FWD(i, opi) {
			ASSERT3P(opi->zpmem_op_name, !=, NULL);
			ASSERT3P(opi->zpmem_op_check_supported, !=, NULL);
			ASSERT3P(opi->zpmem_op_memcpy256_nt_nodrain, !=, NULL);
			ASSERT3P(opi->zpmem_op_memzero256_nt_nodrain, !=, NULL);
			ASSERT3P(opi->zpmem_op_drain, !=, NULL);
			/* no whitespace allowed (for propert printing) */
			for (const char *q = opi->zpmem_op_name; *q != 0; q++) {
				ASSERT0(isspace(*q));
			}
			/* check for name collisions */
			ITER_PMEM_OP_IMPLS_FWD(j, opj) {
				if (i != j && strcmp(opi->zpmem_op_name,
				    opj->zpmem_op_name) == 0) {
					panic("name collision in pmem ops: %s",
					    opi->zpmem_op_name);
				}
			}
		}
	}

	size_t nsup = 0;
	ITER_PMEM_OP_IMPLS_FWD(i, op) {
		op->zpmem_op_supported = op->zpmem_op_check_supported();
		if (op->zpmem_op_supported) nsup++;
	}
	if (nsup == 0) {
		const size_t len = 4096;
		char *buf = vmem_zalloc(len, KM_SLEEP);
		size_t cnt = 0;
		ITER_PMEM_OP_IMPLS_FWD(i, op) {
			cnt += snprintf(buf + cnt, len - cnt, "%s,",
			    op->zpmem_op_name);
		}
		VERIFY3U(cnt, <, len);
		if (cnt > 0)
			buf[cnt-1] = '\0'; /* strip trailing comma */
		dprintf("none of the available PMEM ops impls is supported by "
		    "the CPU: %s\n", buf);
		vmem_free(buf, len);

		ret = SET_ERROR(ENOTSUP);
		goto err0;
	}

	ITER_PMEM_OP_IMPLS_FWD(i, op) {
		if (!op->zpmem_op_supported)
			continue;
		ret = op->zpmem_op_init ? op->zpmem_op_init() : 0;
		if (ret != 0)
			goto err1;
		op->zpmem_op_initialized = B_TRUE;
	}

	/*
	 * Pick first supported impl as default.
	 * XXX auto-detect best
	 */
	zfs_pmem_ops_t *defaultop = NULL;
	ITER_PMEM_OP_IMPLS_FWD(i, op) {
		if (!op->zpmem_op_supported)
			continue;
		VERIFY(op->zpmem_op_initialized);
		defaultop = op;
		break;
	}
	VERIFY3P(defaultop, !=, NULL);
	zfs_pmem_ops_set(defaultop);

	return (ret);

err1:
	/* XXX dedup code with fini */
	ITER_PMEM_OP_IMPLS_FWD(i, op) {
		IMPLY(op->zpmem_op_initialized, op->zpmem_op_supported);
		if (op->zpmem_op_initialized) {
			int err = op->zpmem_op_fini ? op->zpmem_op_fini() : 0;
			VERIFY0(err); /* XXX need handle errors? */
			op->zpmem_op_initialized = B_FALSE;
		}
	}
err0:
	return (ret);
}

void zfs_pmem_ops_fini(void)
{
	size_t i;
	zfs_pmem_ops_t *op;
	ITER_PMEM_OP_IMPLS_FWD(i, op) {
		IMPLY(op->zpmem_op_initialized, op->zpmem_op_supported);
		if (op->zpmem_op_initialized) {
			int err = op->zpmem_op_fini ? op->zpmem_op_fini() : 0;
			VERIFY0(err); /* XXX need handle errors? */
			op->zpmem_op_initialized = B_FALSE;
		}
	}
}


const zfs_pmem_ops_t *
zfs_pmem_ops_get_by_name(const char *val)
{
	size_t i, vlen;
	zfs_pmem_ops_t *op;

	vlen = strlen(val);
	while ((vlen > 0) && !!isspace(val[vlen-1])) /* trim '\n' */
		vlen--;

	zfs_pmem_ops_t *found = NULL;
	ITER_PMEM_OP_IMPLS_FWD(i, op) {
		if (!op->zpmem_op_supported)
			continue;
		const char *name = op->zpmem_op_name;
		if (vlen == strlen(name) && strncmp(val, name, vlen) == 0) {
			found = op;
			break;
		}
	}
	if (found == NULL)
		return (NULL);

	VERIFY(found->zpmem_op_supported);
	VERIFY(found->zpmem_op_initialized);
	return (found);
}


zfs_pmem_ops_t *zfs_pmem_ops_current_impl = NULL;

static inline
const zfs_pmem_ops_t *
zfs_pmem_ops_get_current_impl(void)
{
	zfs_pmem_ops_t* ops =
	    __atomic_load_n(&zfs_pmem_ops_current_impl, __ATOMIC_SEQ_CST);
	ASSERT3P(ops, !=, NULL);
	return (ops);
}

const zfs_pmem_ops_t *
zfs_pmem_ops_get_current(void)
{
	return (zfs_pmem_ops_get_current_impl());
}

const char *
zfs_pmem_ops_name(const zfs_pmem_ops_t *ops)
{
	return (ops->zpmem_op_name);
}

void
zfs_pmem_ops_set(const zfs_pmem_ops_t *arg)
{
	size_t i;
	zfs_pmem_ops_t *op;
	boolean_t found = B_FALSE;
	ITER_PMEM_OP_IMPLS_FWD(i, op) {
		if (op == arg) {
			found = B_TRUE;
			break;
		}
	}
	VERIFY(found);
	VERIFY(arg->zpmem_op_supported);
	VERIFY(arg->zpmem_op_initialized);
	__atomic_store_n(&zfs_pmem_ops_current_impl, arg, __ATOMIC_SEQ_CST);
}


#ifdef __KERNEL__

static int
pmem_ops_param_get(char *buf, zfs_kernel_param_t *unused)
{
	const zfs_pmem_ops_t *cur = zfs_pmem_ops_get_current();
	VERIFY3P(cur, !=, NULL); /* zfs_pmem_ops_init must run before */

	boolean_t found_cur = B_FALSE;
	size_t cnt = 0;
	size_t i;
	zfs_pmem_ops_t *op;
	ITER_PMEM_OP_IMPLS_FWD(i, op) {
		const char *state;
		IMPLY(op->zpmem_op_supported, op->zpmem_op_initialized);
		if (op == cur) {
			VERIFY(op->zpmem_op_supported);
			VERIFY(op->zpmem_op_initialized);
			found_cur = B_TRUE;
			state = "active";
		} else if (op->zpmem_op_supported) {
			state = "supported";
		} else {
			state = "unsupported";
		}
		/* XXX buf has some limited size, we could be overflowing it */
		cnt += sprintf(buf + cnt, "%s\t%s\n", op->zpmem_op_name, state);
	}
	VERIFY(found_cur);

	return (cnt);
}

static int
pmem_ops_param_set(const char *val, zfs_kernel_param_t *unused)
{
	const zfs_pmem_ops_t *ops = zfs_pmem_ops_get_by_name(val);
	if (ops == NULL)
		return (SET_ERROR(ENOENT));
	zfs_pmem_ops_set(ops);
	return (0);
}

/* FIXME settings this as param on insmod is broken, only works afterwards */
/* BEGIN CSTYLED */
ZFS_MODULE_VIRTUAL_PARAM_CALL(zfs, zfs_, pmem_ops_impl,
	pmem_ops_param_set, pmem_ops_param_get, ZMOD_RW,
	"Select PMEM ops implementation.");
/* END CSTYLED */

#endif /* __KERNEL__ */


/*******************************************************************************
 *****************************PUBLIC  FORWARDS**********************************
 ******************************************************************************/

#ifdef __KERNEL__
#include <sys/pmem_spl.h>
int zfs_pmem_memcpy_mcsafe(void *dst, const void *src_pmem, size_t size)
{
	return (spl_memcpy_mc(dst, src_pmem, size));
}
#else  /* __KERNEL__ */
int zfs_pmem_memcpy_mcsafe(void *dst, const void *src_pmem, size_t size)
{
	/*
	 * XXX Handling of SIGBUS:
	 * - register SIGBUS via sigaction() with SA_SIGINFO
	 * - check whether siginfo::si_addr is in a memory area where we
	 *   should convert it into an error
	 *   -> for libzpool: don't even have support for mmapping yet
	 *   -> for zilpmem_test: function that informs zfs_pmem about
	 *      the registered area (tracking via simple (base,len) list)
	 * - If the fault is within the area use longjumps to return the error.
	 *
	 * => https://www.linuxprogrammingblog.com/code-examples/SIGBUS-handling
	 *    as a starting point.
	 */
	memcpy(dst, src_pmem, size); /* will crash on SIGBUS */
	return (0);
}
#endif /* __KERNEL__ */

void zfs_pmem_memcpy256_nt_nodrain(void *dst, const void *src, size_t size,
				   zfs_kfpu_ctx_t *kfpu_ctx)
{
	ASSERT0(((uintptr_t)dst) % 64);
	ASSERT0(size % 4 * 64);
	const zfs_pmem_ops_t *ops = zfs_pmem_ops_get_current_impl();
	ops->zpmem_op_memcpy256_nt_nodrain(dst, src, size, kfpu_ctx);
}

void zfs_pmem_memzero256_nt_nodrain(void *dst, size_t size, zfs_kfpu_ctx_t *kfpu_ctx)
{
	ASSERT0(((uintptr_t)dst) % 64);
	ASSERT0(size % (4 * 64));
	const zfs_pmem_ops_t *ops = zfs_pmem_ops_get_current_impl();
	ops->zpmem_op_memzero256_nt_nodrain(dst, size, kfpu_ctx);
}

void zfs_pmem_drain(void)
{
	const zfs_pmem_ops_t *ops = zfs_pmem_ops_get_current();
	ops->zpmem_op_drain();
}
