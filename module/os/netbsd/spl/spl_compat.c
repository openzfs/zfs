// SPDX-License-Identifier: CDDL-1.0
/*
 * Symbols the OpenZFS core expects that NetBSD does not export to
 * modules (or that we have not yet wired to a native primitive).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kmem.h>
#include <sys/kmem_cache.h>
#include <sys/malloc.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/dirent.h>
#include <sys/proc.h>
#include <sys/atomic.h>
#include <sys/stdarg.h>
#include <sys/sdt.h>
#include <sys/libkern.h>
#include <sha2/sha2_impl.h>

void
solaris_delay(unsigned long ticks)
{
	(void) kpause("soldelay", false, (int)ticks, NULL);
}

/*
 * Heap sort so we do not depend on an unexported qsort(9).
 */
void
spl_qsort(void *base, size_t nmemb, size_t size,
    int (*compar)(const void *, const void *))
{
	char *b = base;
	char tmp[128];
	size_t i, n;
	char *scratch = tmp;

	if (nmemb < 2 || size == 0)
		return;
	if (size > sizeof (tmp))
		scratch = kmem_alloc(size, KM_SLEEP);

	/* heapify */
	for (i = nmemb / 2; i > 0; i--) {
		size_t parent = i - 1;
		for (;;) {
			size_t l = 2 * parent + 1;
			size_t r = l + 1;
			size_t largest = parent;
			if (l < nmemb &&
			    compar(b + l * size, b + largest * size) > 0)
				largest = l;
			if (r < nmemb &&
			    compar(b + r * size, b + largest * size) > 0)
				largest = r;
			if (largest == parent)
				break;
			memcpy(scratch, b + parent * size, size);
			memcpy(b + parent * size, b + largest * size, size);
			memcpy(b + largest * size, scratch, size);
			parent = largest;
		}
	}
	for (n = nmemb; n > 1; n--) {
		memcpy(scratch, b, size);
		memcpy(b, b + (n - 1) * size, size);
		memcpy(b + (n - 1) * size, scratch, size);
		size_t parent = 0;
		for (;;) {
			size_t l = 2 * parent + 1;
			size_t r = l + 1;
			size_t largest = parent;
			if (l < n - 1 &&
			    compar(b + l * size, b + largest * size) > 0)
				largest = l;
			if (r < n - 1 &&
			    compar(b + r * size, b + largest * size) > 0)
				largest = r;
			if (largest == parent)
				break;
			memcpy(scratch, b + parent * size, size);
			memcpy(b + parent * size, b + largest * size, size);
			memcpy(b + largest * size, scratch, size);
			parent = largest;
		}
	}
	if (scratch != tmp)
		kmem_free(scratch, size);
}

long long
strtoq(const char *nptr, char **endptr, int base)
{
	return ((long long)strtoi(nptr, endptr, base,
	    LLONG_MIN, LLONG_MAX, NULL));
}

unsigned long long
strtouq(const char *nptr, char **endptr, int base)
{
	return ((unsigned long long)strtou(nptr, endptr, base,
	    0, ULLONG_MAX, NULL));
}

int
flsll(unsigned long long x)
{
	return (x == 0 ? 0 : 64 - __builtin_clzll(x));
}

void
spl_panic(const char *file, const char *func, int line, const char *fmt, ...)
{
	va_list ap;

	printf("SPL PANIC %s:%s:%d: ", file, func, line);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	panic("spl_panic");
}

void
spl_kmem_cache_set_move(kmem_cache_t *cache,
    kmem_cbrc_t (*func)(void *, void *, size_t, void *))
{
	(void) cache;
	(void) func;
}

/* Minimal xxHash64 so zstd can link without XXH_INLINE_ALL everywhere. */
#define	PRIME64_1	0x9E3779B185EBCA87ULL
#define	PRIME64_2	0xC2B2AE3D27D4EB4FULL
#define	PRIME64_3	0x165667B19E3779F9ULL
#define	PRIME64_4	0x85EBCA77C2B2AE63ULL
#define	PRIME64_5	0x27D4EB2F165667C5ULL

typedef struct {
	uint64_t	total;
	uint64_t	v1, v2, v3, v4;
	uint8_t		mem[32];
	uint32_t	memsize;
} xxh64_state_t;

static uint64_t
xxh_rotl64(uint64_t x, int r)
{
	return ((x << r) | (x >> (64 - r)));
}

static uint64_t
xxh_readle64(const void *p)
{
	const uint8_t *b = p;

	return ((uint64_t)b[0] |
	    ((uint64_t)b[1] << 8) |
	    ((uint64_t)b[2] << 16) |
	    ((uint64_t)b[3] << 24) |
	    ((uint64_t)b[4] << 32) |
	    ((uint64_t)b[5] << 40) |
	    ((uint64_t)b[6] << 48) |
	    ((uint64_t)b[7] << 56));
}

static uint32_t
xxh_readle32(const void *p)
{
	const uint8_t *b = p;

	return ((uint32_t)b[0] |
	    ((uint32_t)b[1] << 8) |
	    ((uint32_t)b[2] << 16) |
	    ((uint32_t)b[3] << 24));
}

static uint64_t
xxh64_round(uint64_t acc, uint64_t input)
{
	acc += input * PRIME64_2;
	acc = xxh_rotl64(acc, 31);
	acc *= PRIME64_1;
	return (acc);
}

static uint64_t
xxh64_merge(uint64_t acc, uint64_t val)
{
	val = xxh64_round(0, val);
	acc ^= val;
	acc = acc * PRIME64_1 + PRIME64_4;
	return (acc);
}

static uint64_t
xxh64_avalanche(uint64_t h)
{
	h ^= h >> 33;
	h *= PRIME64_2;
	h ^= h >> 29;
	h *= PRIME64_3;
	h ^= h >> 32;
	return (h);
}

static uint64_t
xxh64_finalize(uint64_t h, const uint8_t *p, size_t len)
{
	len &= 31;
	while (len >= 8) {
		uint64_t k1 = xxh64_round(0, xxh_readle64(p));

		p += 8;
		len -= 8;
		h ^= k1;
		h = xxh_rotl64(h, 27) * PRIME64_1 + PRIME64_4;
	}
	if (len >= 4) {
		h ^= (uint64_t)xxh_readle32(p) * PRIME64_1;
		p += 4;
		len -= 4;
		h = xxh_rotl64(h, 23) * PRIME64_2 + PRIME64_3;
	}
	while (len > 0) {
		h ^= (*p++) * PRIME64_5;
		h = xxh_rotl64(h, 11) * PRIME64_1;
		len--;
	}
	return (xxh64_avalanche(h));
}

static void
xxh64_acc32(xxh64_state_t *s, const uint8_t *p)
{
	s->v1 = xxh64_round(s->v1, xxh_readle64(p + 0));
	s->v2 = xxh64_round(s->v2, xxh_readle64(p + 8));
	s->v3 = xxh64_round(s->v3, xxh_readle64(p + 16));
	s->v4 = xxh64_round(s->v4, xxh_readle64(p + 24));
}

void
ZSTD_XXH64_reset(void *state, uint64_t seed)
{
	xxh64_state_t *s = state;

	memset(s, 0, sizeof (*s));
	s->v1 = seed + PRIME64_1 + PRIME64_2;
	s->v2 = seed + PRIME64_2;
	s->v3 = seed + 0;
	s->v4 = seed - PRIME64_1;
}

void
ZSTD_XXH64_update(void *state, const void *input, size_t len)
{
	xxh64_state_t *s = state;
	const uint8_t *p = input;
	const uint8_t *end;

	if (p == NULL || len == 0)
		return;
	end = p + len;
	s->total += len;

	if (s->memsize + len < 32) {
		memcpy(s->mem + s->memsize, p, len);
		s->memsize += (uint32_t)len;
		return;
	}
	if (s->memsize != 0) {
		size_t fill = 32 - s->memsize;

		memcpy(s->mem + s->memsize, p, fill);
		xxh64_acc32(s, s->mem);
		p += fill;
		s->memsize = 0;
	}
	if (p + 32 <= end) {
		const uint8_t *limit = end - 32;

		do {
			xxh64_acc32(s, p);
			p += 32;
		} while (p <= limit);
	}
	if (p < end) {
		size_t n = (size_t)(end - p);

		memcpy(s->mem, p, n);
		s->memsize = (uint32_t)n;
	}
}

uint64_t
ZSTD_XXH64_digest(const void *state)
{
	const xxh64_state_t *s = state;
	uint64_t h;

	if (s->total >= 32) {
		h = xxh_rotl64(s->v1, 1) + xxh_rotl64(s->v2, 7) +
		    xxh_rotl64(s->v3, 12) + xxh_rotl64(s->v4, 18);
		h = xxh64_merge(h, s->v1);
		h = xxh64_merge(h, s->v2);
		h = xxh64_merge(h, s->v3);
		h = xxh64_merge(h, s->v4);
	} else {
		h = s->v3 + PRIME64_5;
	}
	h += s->total;
	return (xxh64_finalize(h, s->mem, s->total));
}

uint64_t
ZSTD_XXH64(const void *input, size_t len, uint64_t seed)
{
	xxh64_state_t s;

	ZSTD_XXH64_reset(&s, seed);
	ZSTD_XXH64_update(&s, input, len);
	return (ZSTD_XXH64_digest(&s));
}

#ifdef Z_OK
#else
/* provided by zlib.c when that is compiled into the module */
#if !defined(HAVE_KERNEL_ZLIB_ZERROR)
const char *
zError(int err)
{
	if (err == 0)
		return ("Z_OK");
	return ("zlib-error");
}
#endif
#endif

/*
 * Weak SIMD fallbacks -- only when the real .S files are not in the kmod.
 * HAVE_BLAKE3_*_ASM is set from Makefile.openzfs next to the SRCS
 * lists so the stub set cannot drift from what is actually assembled.
 */
#if !defined(__amd64__) && !defined(__x86_64__)
#define	BLAKE3_STUB_INPLACE(name) \
void \
name(uint32_t cv[8], const uint8_t block[64], uint8_t block_len, \
    uint64_t counter, uint8_t flags) \
{ \
	(void) cv; (void) block; (void) block_len; (void) counter; \
	(void) flags; \
	panic(#name " SIMD not wired"); \
}

#define	BLAKE3_STUB_XOF(name) \
void \
name(const uint32_t cv[8], const uint8_t block[64], uint8_t block_len, \
    uint64_t counter, uint8_t flags, uint8_t out[64]) \
{ \
	(void) cv; (void) block; (void) block_len; (void) counter; \
	(void) flags; (void) out; \
	panic(#name " SIMD not wired"); \
}

#define	BLAKE3_STUB_MANY(name) \
void \
name(const uint8_t * const *inputs, size_t num_inputs, size_t blocks, \
    const uint32_t key[8], uint64_t counter, boolean_t increment_counter, \
    uint8_t flags, uint8_t flags_start, uint8_t flags_end, uint8_t *out) \
{ \
	(void) inputs; (void) num_inputs; (void) blocks; (void) key; \
	(void) counter; (void) increment_counter; (void) flags; \
	(void) flags_start; (void) flags_end; (void) out; \
	panic(#name " SIMD not wired"); \
}

#ifndef HAVE_BLAKE3_SSE2_ASM
BLAKE3_STUB_INPLACE(zfs_blake3_compress_in_place_sse2)
BLAKE3_STUB_INPLACE(zfs_blake3_compress_in_place_sse41)
BLAKE3_STUB_XOF(zfs_blake3_compress_xof_sse2)
BLAKE3_STUB_XOF(zfs_blake3_compress_xof_sse41)
BLAKE3_STUB_MANY(zfs_blake3_hash_many_sse2)
BLAKE3_STUB_MANY(zfs_blake3_hash_many_sse41)
#endif
#ifndef HAVE_BLAKE3_AVX_ASM
BLAKE3_STUB_INPLACE(zfs_blake3_compress_in_place_avx512)
BLAKE3_STUB_XOF(zfs_blake3_compress_xof_avx512)
BLAKE3_STUB_MANY(zfs_blake3_hash_many_avx2)
BLAKE3_STUB_MANY(zfs_blake3_hash_many_avx512)
#endif

#undef	BLAKE3_STUB_INPLACE
#undef	BLAKE3_STUB_XOF
#undef	BLAKE3_STUB_MANY

/* Baseline x64 transforms: dispatch to the portable C generic impl. */
extern const sha256_ops_t sha256_generic_impl;
extern const sha512_ops_t sha512_generic_impl;

void
zfs_sha256_transform_x64(uint32_t s[8], const void *d, size_t b)
{
	sha256_generic_impl.transform(s, d, b);
}

void
zfs_sha256_transform_ssse3(uint32_t s[8], const void *d, size_t b)
{
	sha256_generic_impl.transform(s, d, b);
}

void
zfs_sha256_transform_avx(uint32_t s[8], const void *d, size_t b)
{
	sha256_generic_impl.transform(s, d, b);
}

void
zfs_sha256_transform_avx2(uint32_t s[8], const void *d, size_t b)
{
	sha256_generic_impl.transform(s, d, b);
}

void
zfs_sha256_transform_shani(uint32_t s[8], const void *d, size_t b)
{
	sha256_generic_impl.transform(s, d, b);
}

void
zfs_sha512_transform_x64(uint64_t s[8], const void *d, size_t b)
{
	sha512_generic_impl.transform(s, d, b);
}

void
zfs_sha512_transform_avx(uint64_t s[8], const void *d, size_t b)
{
	sha512_generic_impl.transform(s, d, b);
}

void
zfs_sha512_transform_avx2(uint64_t s[8], const void *d, size_t b)
{
	sha512_generic_impl.transform(s, d, b);
}

void
zfs_sha512_transform_sha512ext(uint64_t s[8], const void *d, size_t b)
{
	sha512_generic_impl.transform(s, d, b);
}
#endif /* !amd64 */

#undef SET_ERROR
#undef WRITE_ONCE
#undef READ_ONCE
#undef atomic_load_ptr
#undef atomic_store_ptr
#undef atomic_store_rel_ptr
#undef atomic_fetchadd_int
#undef ASSERT_VOP_LOCKED
#undef ASSERT_VOP_ELOCKED
#undef VNASSERT
#undef IS_DEVVP

/*
 * Fallbacks for call sites compiled before the header macros landed.
 */
int
SET_ERROR(int err)
{
	return (err);
}

void
WRITE_ONCE(void *p, void *v)
{
	*(void *volatile *)p = v;
}

void *
READ_ONCE(void *p)
{
	return (*(void *volatile *)p);
}

void *
atomic_load_ptr(void *p)
{
	return (__atomic_load_n((void **)p, __ATOMIC_SEQ_CST));
}

void
atomic_store_ptr(void *p, void *v)
{
	__atomic_store_n((void **)p, v, __ATOMIC_SEQ_CST);
}

void
atomic_store_rel_ptr(void *p, void *v)
{
	__atomic_store_n((void **)p, v, __ATOMIC_RELEASE);
}

int
atomic_fetchadd_int(volatile int *p, int v)
{
	return (__atomic_fetch_add(p, v, __ATOMIC_SEQ_CST));
}

int
ASSERT_VOP_LOCKED(void *vp, const char *w)
{
	(void) vp;
	(void) w;
	return (0);
}

int
ASSERT_VOP_ELOCKED(void *vp, const char *w)
{
	(void) vp;
	(void) w;
	return (0);
}

void
VNASSERT(int exp, void *vp, const char *msg)
{
	(void) exp;
	(void) vp;
	(void) msg;
}

int
IS_DEVVP(void *vp)
{
	(void) vp;
	return (0);
}

int
__hide_VOP_UNLOCK(struct vnode *vp)
{
	return (netbsd_vop_unlock(vp));
}

#ifdef __aarch64__
/*
 * gcc -moutline-atomics (default on many aarch64 toolchains) emits
 * calls into libgcc.  A kmod cannot resolve those.  Makefile.inc
 * also passes -mno-outline-atomics; this is the belt.
 */
uint32_t
__aarch64_ldadd4_acq_rel(volatile uint32_t *ptr, uint32_t val)
{
	return (atomic_add_32_nv(ptr, val) - val);
}

uint64_t
__aarch64_ldadd8_acq_rel(volatile uint64_t *ptr, uint64_t val)
{
	return (atomic_add_64_nv(ptr, val) - val);
}
#endif

/* zlib module exports inflateInit2_/deflateInit2_, not the Init_ wrappers. */
int inflateInit2_(void *, int, const char *, int);
int deflateInit2_(void *, int, int, int, int, int, const char *, int);

int
inflateInit_(void *strm, const char *version, int stream_size)
{
	return (inflateInit2_(strm, 15, version, stream_size));
}

int
deflateInit_(void *strm, int level, const char *version, int stream_size)
{
	return (deflateInit2_(strm, level, 8, 15, 8, 0, version, stream_size));
}
