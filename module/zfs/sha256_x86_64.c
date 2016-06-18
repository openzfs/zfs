#include <linux/kernel.h>
#include <linux/simd_x86.h>
#include <sys/sha256.h>

#define	KFPU_WRAPPER(type)						\
static void								\
sha256_transform_##type(const void *buf, uint32_t *H, uint64_t blks)	\
{									\
	kfpu_begin();							\
	sha256_transform_##type##_raw(buf, H, blks);			\
	kfpu_end();							\
}

#ifdef HAVE_SSSE3
void sha256_transform_ssse3_raw(const void *, uint32_t *, uint64_t);
KFPU_WRAPPER(ssse3);
#endif
#ifdef HAVE_AVX
void sha256_transform_avx_raw(const void *, uint32_t *, uint64_t);
KFPU_WRAPPER(avx);
#endif
#ifdef HAVE_AVX2
void sha256_transform_rorx_raw(const void *, uint32_t *, uint64_t);
KFPU_WRAPPER(rorx);
#endif

#ifdef HAVE_SSSE3
static int
ssse3_test(void)
{
	return (zfs_ssse3_available());
}
#endif

#ifdef HAVE_AVX
static int
avx_test(void)
{
	return (zfs_avx_available());
}
#endif

#ifdef HAVE_AVX2
static int
avx2_test(void)
{
	return (zfs_avx2_available() && zfs_bmi2_available());
}
#endif

struct sha256_algo sha256_algos[] = {
	{ "generic", NULL, sha256_transform_generic },
#ifdef HAVE_SSSE3
	{ "ssse3", ssse3_test, sha256_transform_ssse3 },
#endif
#ifdef HAVE_AVX
	{ "avx", avx_test, sha256_transform_avx },
#endif
#ifdef HAVE_AVX2
	{ "avx2", avx2_test, sha256_transform_rorx },
#endif
	{ NULL, NULL, NULL },
};
