/*
 * Xorshift Pseudo Random Number Generator based on work by David Blackman
 * and Sebastiano Vigna (vigna@acm.org).
 *
 *   "Further scramblings of Marsaglia's xorshift generators"
 *   http://vigna.di.unimi.it/ftp/papers/xorshiftplus.pdf
 *   http://prng.di.unimi.it/xoroshiro128plusplus.c
 *
 * To the extent possible under law, the author has dedicated all copyright
 * and related and neighboring rights to this software to the public domain
 * worldwide. This software is distributed without any warranty.
 *
 * See <http://creativecommons.org/publicdomain/zero/1.0/>.
 *
 * This is xoroshiro128++ 1.0, one of our all-purpose, rock-solid,
 * small-state generators. It is extremely (sub-ns) fast and it passes all
 * tests we are aware of, but its state space is large enough only for
 * mild parallelism.
 */

#include <sys/vdev_draid.h>

static inline uint64_t rotl(const uint64_t x, int k)
{
	return (x << k) | (x >> (64 - k));
}

uint64_t
vdev_draid_rand(uint64_t *s)
{
	const uint64_t s0 = s[0];
	uint64_t s1 = s[1];
	const uint64_t result = rotl(s0 + s1, 17) + s0;

	s1 ^= s0;
	s[0] = rotl(s0, 49) ^ s1 ^ (s1 << 21); // a, b
	s[1] = rotl(s1, 28); // c

	return (result);
}
