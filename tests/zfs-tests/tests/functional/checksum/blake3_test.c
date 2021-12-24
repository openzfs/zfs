
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2013 Saso Kiselkov. All rights reserved.
 * Copyright (c) 2021 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#ifdef	_KERNEL
#undef	_KERNEL
#endif

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/blake3.h>

#define	NOTE(x)

/*
 * Test messages are done via rust reference: b3sum
 */

const char	*test_msg0 = "abc";
const char	*test_msg1 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmn"
	"lmnomnopnopq";
const char	*test_msg2 = "abcdefghbcdefghicdefghijdefghijkefghijklfghi"
	"jklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

/*
 * Test digests from:
 * http://csrc.nist.gov/groups/ST/toolkit/documents/Examples/SHA_All.pdf
 */
const uint8_t	blake3_256_test_digests[][32] = {
	{
		/* for test_msg0 */
		0x64, 0x37, 0xb3, 0xac, 0x38, 0x46, 0x51, 0x33, 0xff, 0xb6,
		0x3b, 0x75, 0x27, 0x3a, 0x8d, 0xb5, 0x48, 0xc5, 0x58, 0x46,
		0x5d, 0x79, 0xdb, 0x03, 0xfd, 0x35, 0x9c, 0x6c, 0xd5, 0xbd,
		0x9d, 0x85
	},
	{
		/* for test_msg1 */
		0xc1, 0x90, 0x12, 0xcc, 0x2a, 0xaf, 0x0d, 0xc3, 0xd8, 0xe5,
		0xc4, 0x5a, 0x1b, 0x79, 0x11, 0x4d, 0x2d, 0xf4, 0x2a, 0xbb,
		0x2a, 0x41, 0x0b, 0xf5, 0x4b, 0xe0, 0x9e, 0x89, 0x1a, 0xf0,
		0x6f, 0xf8
	},
	{
		/* for test_msg2 */
		0x55, 0x3e, 0x1a, 0xa2, 0xa4, 0x77, 0xcb, 0x31, 0x66, 0xe6,
		0xab, 0x38, 0xc1, 0x2d, 0x59, 0xf6, 0xc5, 0x01, 0x7f, 0x08,
		0x85, 0xaa, 0xf0, 0x79, 0xf2, 0x17, 0xda, 0x00, 0xcf, 0xca,
		0x36, 0x3f
	}
};

int
main(int argc, char *argv[])
{
	boolean_t	failed = B_FALSE;
	uint64_t	cpu_mhz = 0;

	if (argc == 2)
		cpu_mhz = atoi(argv[1]);

#define	BLAKE3_ALGO_TEST(_m, mode, diglen, testdigest)			\
	do {								\
		BLAKE3_CTX		ctx;				\
		uint8_t			digest[diglen / 8];		\
		Blake3_Init(&ctx);					\
		Blake3_Update(&ctx, _m, strlen(_m));			\
		Blake3_Final(&ctx, digest);				\
		(void) printf("BLAKE3%-9sMessage: " #_m			\
		    "\tResult: ", #mode);				\
		if (bcmp(digest, testdigest, diglen / 8) == 0) {	\
			(void) printf("OK\n");				\
		} else {						\
			(void) printf("FAILED!\n");			\
			failed = B_TRUE;				\
		}							\
		NOTE(CONSTCOND)						\
	} while (0)

#define	BLAKE3_PERF_TEST(mode, diglen)					\
	do {								\
		BLAKE3_CTX	ctx;					\
		uint8_t		digest[diglen / 8];			\
		uint8_t		block[131072];				\
		uint64_t	delta;					\
		double		cpb = 0;				\
		int		i;					\
		struct timeval	start, end;				\
		bzero(block, sizeof (block));				\
		(void) gettimeofday(&start, NULL);			\
		Blake3_Init(&ctx);					\
		for (i = 0; i < 8192; i++)				\
			Blake3_Update(&ctx, block, sizeof (block));	\
		Blake3_Final(&ctx, digest);				\
		(void) gettimeofday(&end, NULL);			\
		delta = (end.tv_sec * 1000000llu + end.tv_usec) -	\
		    (start.tv_sec * 1000000llu + start.tv_usec);	\
		if (cpu_mhz != 0) {					\
			cpb = (cpu_mhz * 1e6 * ((double)delta /		\
			    1000000)) / (8192 * 128 * 1024);		\
		}							\
		(void) printf("BLAKE3%-9s%llu us (%.02f CPB)\n", #mode,	\
		    (u_longlong_t)delta, cpb);				\
		NOTE(CONSTCOND)						\
	} while (0)

	(void) printf("Running algorithm correctness tests:\n");
	BLAKE3_ALGO_TEST(test_msg0, 256, 256, blake3_256_test_digests[0]);
	BLAKE3_ALGO_TEST(test_msg1, 256, 256, blake3_256_test_digests[1]);
	BLAKE3_ALGO_TEST(test_msg2, 256, 256, blake3_256_test_digests[2]);

	if (failed)
		return (1);

	(void) printf("Running performance tests (hashing 1024 MiB of "
	    "data):\n");
	BLAKE3_PERF_TEST(256, 256);

	return (0);
}
