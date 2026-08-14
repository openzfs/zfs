// SPDX-License-Identifier: CDDL-1.0
#ifndef _OPENSOLARIS_SYS_RANDOM_H_
#define	_OPENSOLARIS_SYS_RANDOM_H_

#include_next <sys/random.h>
#ifdef _KERNEL
#include <sys/cprng.h>
#else
#include <errno.h>
#include <unistd.h>
#endif

static inline int
random_get_bytes(uint8_t *p, size_t s)
{
#ifdef _KERNEL
	/*
	 * Master keys, wrapping IVs, per-block IVs, HKDF salts.
	 * kern_cprng is IPL_VM (safe in zio).  NetBSD 11's
	 * cprng_strong() KASSERTs flags==0 -- there is no
	 * CPRNG_TRY_ANY.  Chunk at CPRNG_MAX_LEN.
	 */
	while (s > 0) {
		size_t n = s;

		if (n > CPRNG_MAX_LEN)
			n = CPRNG_MAX_LEN;
		if (cprng_strong(kern_cprng, p, n, 0) != n)
			return (EIO);
		p += n;
		s -= n;
	}
	return (0);
#else
	/*
	 * NetBSD getrandom(2) is the supported CSPRNG.  A successful
	 * no-op would leave callers (checksum salts, dataset GUIDs)
	 * with uninitialized or leftover bytes.
	 */
	while (s > 0) {
		ssize_t n = getrandom(p, s, 0);

		if (n < 0) {
			if (errno == EINTR)
				continue;
			return (errno);
		}
		if (n == 0)
			return (EIO);
		p += (size_t)n;
		s -= (size_t)n;
	}
	return (0);
#endif
}

static inline int
random_get_pseudo_bytes(uint8_t *p, size_t s)
{
#ifdef _KERNEL
	cprng_fast(p, s);
	return (0);
#else
	return (random_get_bytes(p, s));
#endif
}

static inline uint32_t
random_in_range(uint32_t range)
{
	if (range <= 1)
		return (0);
#ifdef _KERNEL
	return (cprng_fast32() % range);
#else
	uint32_t r;

	if (random_get_pseudo_bytes((uint8_t *)&r, sizeof (r)) != 0)
		return (0);
	return (r % range);
#endif
}

#endif	/* !_OPENSOLARIS_SYS_RANDOM_H_ */
