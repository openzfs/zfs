#ifndef	_SYS_SHA256_H
#define	_SYS_SHA256_H

#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	SHA256_SHIFT	(6)
#define	SHA256_BLOCK	(1<<SHA256_SHIFT)

struct sha256_algo {
	const char *name;
	int (*test)(void);
	void (*func)(const void *, uint32_t *, uint64_t);
};

/* Arch specific algo table */
extern struct sha256_algo sha256_algos[];

/* generic algorithm, every arch should include this */
void sha256_transform_generic(const void *, uint32_t *, uint64_t);

/*
 * Each arch which has custom sha256_algos should define
 * ARCH_HAS_SHA256_ALGOS.
 */
#if defined(__x86_64) && defined(_KERNEL)
#define	ARCH_HAS_SHA256_ALGOS
#endif

extern void zio_checksum_SHA256_init(void);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_SHA256_H */
