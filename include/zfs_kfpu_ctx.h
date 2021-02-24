#ifndef	_ZFS_KFPU_CTX_H_
#define	_ZFS_KFPU_CTX_H_

typedef struct zfs_kfpu_ctx {
	int zkfpu_count;
} zfs_kfpu_ctx_t;

static inline void
zfs_kfpu_ctx_init(zfs_kfpu_ctx_t *ctx)
{
	ctx->zkfpu_count = 0;
}

static inline boolean_t
zfs_kfpu_ctx_held(zfs_kfpu_ctx_t *ctx)
{
	return (ctx->zkfpu_count > 0);
}

static inline void
zfs_kfpu_enter(zfs_kfpu_ctx_t *ctx)
{
	if (++ctx->zkfpu_count == 1)
		kfpu_begin();
}

static inline void
zfs_kfpu_exit(zfs_kfpu_ctx_t *ctx)
{
	if (--ctx->zkfpu_count == 0)
		kfpu_end();
}


#endif /* _ZFS_KFPU_CTX_H_ */
