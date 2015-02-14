#ifndef	_SYS_SHA256_H
#define	_SYS_SHA256_H

#include <sys/zfs_context.h>
#include <asm/sha256.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	SHA256_SHIFT	(6)
#define	SHA256_BLOCK	(1<<SHA256_SHIFT)

extern void (*sha256_transform)(const void *, uint32_t *, uint64_t);
extern void sha256_transform_generic(const void *, uint32_t *, uint64_t);

extern void zio_checksum_SHA256_init(void);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_SHA256_H */
