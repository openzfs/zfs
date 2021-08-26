#ifndef _ZIL_PMEM_IMPL_LIMITS_H_
#define _ZIL_PMEM_IMPL_LIMITS_H_

#include <sys/zfs_context.h>
#include <sys/zil_impl.h>

typedef struct zilog_pmem_limits {
	uint64_t zlplim_prb_min_chunk_size; /* XXX this should be a parameter */
	uint64_t zlplim_max_lr_write_lr_length;
	uint64_t zlplim_read_maxreclen;
} zilog_pmem_limits_t;

static inline uint64_t
zlp_limits_max_lr_write_lrlength_on_write(zilog_pmem_limits_t limits)
{
	return (limits.zlplim_max_lr_write_lr_length);
}

static inline uint64_t
zlp_limits_max_lr_write_reclen_on_write(zilog_pmem_limits_t limits)
{
	return (sizeof(lr_write_t) + zlp_limits_max_lr_write_lrlength_on_write(limits));
}

static inline uint64_t
zlp_limits_max_lr_reclen_on_write(zilog_pmem_limits_t limits)
{
	uint64_t max_lrw = zlp_limits_max_lr_write_reclen_on_write(limits);
	uint64_t max_other_lrs = 1 << 17; /* XXX determine the maximum lr_t reclen for all other lr_ts */
	return MAX(max_lrw, max_other_lrs);
}

static inline uint64_t
zlp_limits_max_lr_reclen_on_read(zilog_pmem_limits_t limits)
{
	return (limits.zlplim_read_maxreclen);
}

#define ZLPLIMITCHECK(errfn, lhs, op, rhs, what)                                                                         \
	do                                                                                                               \
	{                                                                                                                \
		boolean_t (*_errfn)(char *fmt, ...) = errfn;                                                             \
		if (!((lhs)op(rhs)))                                                                                     \
		{                                                                                                        \
			_errfn("limit check failed: %s: %s %s %s (%llu %s %llu)", what, #lhs, #op, #rhs, lhs, #op, rhs); \
		}                                                                                                        \
	} while (0);

#define ZLPLIMITCHECKFN(fnname, rtype, okretval, errfn)                    \
	static rtype fnname(zilog_pmem_limits_t limits)                    \
	{                                                                  \
		ZLPLIMITCHECK(                                             \
		    errfn,                                                 \
		    zlp_limits_max_lr_reclen_on_read(limits), >=,          \
		    zlp_limits_max_lr_write_reclen_on_write(limits),       \
		    "read our own writes");                                \
                                                                           \
		ZLPLIMITCHECK(                                             \
		    errfn,                                                 \
		    zlp_limits_max_lr_reclen_on_write(limits),             \
		    <=,                                                    \
		    SPA_MAXBLOCKSIZE,                                      \
		    "allocate zl_commit_lr_buf using zio_data_buf_alloc"); \
                                                                           \
		return okretval;                                           \
	}


#endif /* _ZIL_PMEM_IMPL_LIMITS_H_ */
