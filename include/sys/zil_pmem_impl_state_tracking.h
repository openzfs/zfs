#ifndef _ZIL_PMEM_IMPL_STATE_TRACKING_H_
#define _ZIL_PMEM_IMPL_STATE_TRACKING_H_

typedef struct zilog_pmem zilog_pmem_t;

#include <sys/zfs_context.h>
#include <sys/rrwlock.h>

static const char *
zilog_pmem_state_to_str(zilog_pmem_state_t st, boolean_t *invalid)
{
	if (invalid)
		*invalid = B_FALSE;

	switch (st) {
	case ZLP_ST_WAITCLAIMORCLEAR:
		return "WAITCLAIMORCLEAR";
	case ZLP_ST_CLAIMING:
		return "CLAIMING";
	case ZLP_ST_CLAIMING_FAILED:
		return "CLAIMING_FAILED";
	case ZLP_ST_CLOSED:
		return "CLOSED";
	case ZLP_ST_CLOSING:
		return "CLOSING";
	case ZLP_ST_SNAPSHOT:
		return "SNAPSHOT";
	case ZLP_ST_O_WAIT_REPLAY_OR_DESTROY:
		return "O_WAIT_REPLAY_OR_DESTROY";
	case ZLP_ST_O_REPLAYING:
		return "O_REPLAYING";
	case ZLP_ST_O_DESTROYING:
		return "O_DESTROYING";
	case ZLP_ST_O_LOGGING:
		return "O_LOGGING";
	case ZLP_ST_SYNCDESTROYED:
		return "SYNCDESTROYED";
	case ZLP_ST_DESTRUCTED:
		return "DESTRUCTED";
	default:
		if (invalid)
			*invalid = B_TRUE;
		return "<invalid>";
	}
}

/* Returns length without trailing 0 byte */
static int
zilog_pmem_stateset_to_string(zilog_pmem_state_t stateset, char *buf, size_t n)
{
	const size_t nbits = sizeof(stateset) * 8;
	size_t len = 0;

	len += snprintf(buf + len, n - len, "0x%x(", stateset);

	for (size_t i = 0; i < nbits; i++)
	{
		if ((stateset & (1 << i)) == 0)
			continue;
		const char *str = zilog_pmem_state_to_str(1 << i, NULL);
		boolean_t has_more = B_FALSE;
		for (size_t j = i + 1; !has_more && j < nbits; j++)
		{
			has_more = (stateset & (1 << j)) != 0;
		}
		if (has_more)
			len += snprintf(buf + len, n - len, "%s, ", str);
		else
			len += snprintf(buf + len, n - len, "%s)", str);
	}
	len += snprintf(buf + len, n - len, ")");
	/* XXX replace end with ... if we detect insufficient buffer space */

	ASSERT3U(len, <, n);
	ASSERT0(buf[len]);
	return (len);
}

boolean_t zilpmem_state_in(zilog_pmem_state_t st, zilog_pmem_state_t set);
void assert_zilpmem_state_in(zilog_pmem_state_t st, zilog_pmem_state_t set);

static inline void
zilpmem_st_assert(zilog_pmem_state_t is, zilog_pmem_state_t acceptable,
		  const char *file, int line)
{
	if (unlikely((is & acceptable) == 0))
	{
		char is_s[256];
		zilog_pmem_stateset_to_string(is, is_s, sizeof(is_s));
		char acceptable_s[256];
		zilog_pmem_stateset_to_string(acceptable, acceptable_s,
					      sizeof(acceptable_s));

		panic("unacceptable state in %s:%d: is=%s acceptable=%s",
		      file, line, is_s, acceptable_s);
	}
}

static inline void
_zilpmem_st_enter(zilog_pmem_t *zl, zilog_pmem_state_t acceptable, void *tag,
		  const char *file, int line)
{
	rrm_enter_read(&zl->zl_stl, tag);
	zilpmem_st_assert(zl->zl_st, acceptable, file, line);
}

#ifdef ZFS_DEBUG
#define zilpmem_st_enter(zl, a, t) \
	_zilpmem_st_enter(zl, a, t, __FILE__, __LINE__)
#else
#define zilpmem_st_enter(zl, a, t)
#endif

static inline boolean_t
zilpmem_st_held(zilog_pmem_t *zl)
{
	return (rrm_held(&zl->zl_stl, RW_READER));
}

static void
zilpmem_st_upd_impl(zilog_pmem_t *zl, zilog_pmem_state_t st)
{
	ASSERT3U((st & ZLP_ST_ANY), !=, 0);    /* only defined states */
	ASSERT3U((st & (~ZLP_ST_ANY)), ==, 0); /* no undefined bits */
	/* XXX ASSERT3U(__builtin_popcountl(st), ==, 1); exactly one */
	if (zfs_flags & ZFS_DEBUG_ZIL_PMEM)
	{
		/* XXX dbgmsg */
		char cur_s[256];
		zilog_pmem_stateset_to_string(zl->zl_st, cur_s, sizeof(cur_s));
		char st_s[256];
		zilog_pmem_stateset_to_string(st, st_s, sizeof(st_s));

		/* XXX dmu_objset_name crashes when called from zilpmem_dtor */
		char name[ZFS_MAX_DATASET_NAME_LEN];
		if ((st & ZLP_ST_DESTRUCTED) == 0)
		{
			dmu_objset_name(zl->zl_super.zl_os, name);
		}
		else
		{
			snprintf(name, sizeof(name), "???");
		}
#ifdef __KERNEL__
		printk(KERN_INFO "%s (0x%p): updating state from %s to %s\n",
		       name, zl, cur_s, st_s);
#endif
	}
	zl->zl_st = st;
}

static inline void
zilpmem_st_upd(zilog_pmem_t *zl, zilog_pmem_state_t st)
{
	ASSERT(zilpmem_st_held(zl));
	zilpmem_st_upd_impl(zl, st);
}

static inline void
_zilpmem_st_exit(zilog_pmem_t *zl, zilog_pmem_state_t acceptable, void *tag,
		 const char *file, int line)
{
	ASSERT(zilpmem_st_held(zl));
	zilpmem_st_assert(zl->zl_st, acceptable, file, line);
	rrm_exit(&zl->zl_stl, tag);
}

#ifdef ZFS_DEBUG
#define zilpmem_st_exit(zl, a, t) \
	_zilpmem_st_exit(zl, a, t, __FILE__, __LINE__)
#else
#define zilpmem_st_exit(zl, a, t)
#endif

#endif /* _ZIL_PMEM_IMPL_STATE_TRACKING_H_ */
