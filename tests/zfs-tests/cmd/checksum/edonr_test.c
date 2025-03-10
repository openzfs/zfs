// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://opensource.org/licenses/CDDL-1.0.
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
 * Copyright 2013 Saso Kiselkov. All rights reserved.
 */

/*
 * This is just to keep the compiler happy about sys/time.h not declaring
 * gettimeofday due to -D_KERNEL (we can do this since we're actually
 * running in userspace, but we need -D_KERNEL for the remaining Edon-R code).
 */

#include <sys/edonr.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/stdtypes.h>

/*
 * Test messages from:
 * http://csrc.nist.gov/groups/ST/toolkit/documents/Examples/SHA_All.pdf
 */
static const char *test_msg0 = "abc";
static const char *test_msg1 = "abcdefghbcdefghicdefghijdefghijkefghijklfgh"
	"ijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

static const uint8_t	edonr_512_test_digests[][64] = {
	{
		/* for test_msg0 */
		0x1b, 0x14, 0xdb, 0x15, 0x5f, 0x1d, 0x40, 0x65,
		0x94, 0xb8, 0xce, 0xf7, 0x0a, 0x43, 0x62, 0xec,
		0x6b, 0x5d, 0xe6, 0xa5, 0xda, 0xf5, 0x0e, 0xc9,
		0x99, 0xe9, 0x87, 0xc1, 0x9d, 0x30, 0x49, 0xe2,
		0xde, 0x59, 0x77, 0xbb, 0x05, 0xb1, 0xbb, 0x22,
		0x00, 0x50, 0xa1, 0xea, 0x5b, 0x46, 0xa9, 0xf1,
		0x74, 0x0a, 0xca, 0xfb, 0xf6, 0xb4, 0x50, 0x32,
		0xad, 0xc9, 0x0c, 0x62, 0x83, 0x72, 0xc2, 0x2b
	},
	{
		/* no test vector for test_msg1 */
		0
	},
	{
		/* for test_msg1 */
		0x53, 0x51, 0x07, 0x0d, 0xc5, 0x1c, 0x3b, 0x2b,
		0xac, 0xa5, 0xa6, 0x0d, 0x02, 0x52, 0xcc, 0xb4,
		0xe4, 0x92, 0x1a, 0x96, 0xfe, 0x5a, 0x69, 0xe7,
		0x6d, 0xad, 0x48, 0xfd, 0x21, 0xa0, 0x84, 0x5a,
		0xd5, 0x7f, 0x88, 0x0b, 0x3e, 0x4a, 0x90, 0x7b,
		0xc5, 0x03, 0x15, 0x18, 0x42, 0xbb, 0x94, 0x9e,
		0x1c, 0xba, 0x74, 0x39, 0xa6, 0x40, 0x9a, 0x34,
		0xb8, 0x43, 0x6c, 0xb4, 0x69, 0x21, 0x58, 0x3c
	}
};

int
main(int argc, char *argv[])
{
	boolean_t	failed = B_FALSE;
	uint64_t	cpu_mhz = 0;

	if (argc == 2)
		cpu_mhz = atoi(argv[1]);

#define	EDONR_ALGO_TEST(_m, mode, testdigest)				\
	do {								\
		EdonRState	ctx;					\
		uint8_t		digest[mode / 8];			\
		EdonRInit(&ctx);					\
		EdonRUpdate(&ctx, (const uint8_t *) _m, strlen(_m) * 8);\
		EdonRFinal(&ctx, digest);				\
		(void) printf("Edon-R-%-6sMessage: " #_m		\
		    "\tResult: ", #mode);				\
		if (memcmp(digest, testdigest, mode / 8) == 0) {	\
			(void) printf("OK\n");				\
		} else {						\
			(void) printf("FAILED!\n");			\
			failed = B_TRUE;				\
		}							\
	} while (0)

#define	EDONR_PERF_TEST(mode)						\
	do {								\
		EdonRState	ctx;					\
		uint8_t		digest[mode / 8];			\
		uint8_t		block[131072];				\
		uint64_t	delta;					\
		double		cpb = 0;				\
		int		i;					\
		struct timeval	start, end;				\
		memset(block, 0, sizeof (block));			\
		(void) gettimeofday(&start, NULL);			\
		EdonRInit(&ctx);					\
		for (i = 0; i < 8192; i++)				\
			EdonRUpdate(&ctx, block, sizeof (block) * 8);	\
		EdonRFinal(&ctx, digest);				\
		(void) gettimeofday(&end, NULL);			\
		delta = (end.tv_sec * 1000000llu + end.tv_usec) -	\
		    (start.tv_sec * 1000000llu + start.tv_usec);	\
		if (cpu_mhz != 0) {					\
			cpb = (cpu_mhz * 1e6 * ((double)delta /		\
			    1000000)) / (8192 * 128 * 1024);		\
		}							\
		(void) printf("Edon-R-%-6s%llu us (%.02f CPB)\n", #mode,\
		    (u_longlong_t)delta, cpb);				\
	} while (0)

	(void) printf("Running algorithm correctness tests:\n");
	EDONR_ALGO_TEST(test_msg0, 512, edonr_512_test_digests[0]);
	EDONR_ALGO_TEST(test_msg1, 512, edonr_512_test_digests[2]);
	if (failed)
		return (1);

	(void) printf("Running performance tests (hashing 1024 MiB of "
	    "data):\n");
	EDONR_PERF_TEST(512);

	return (0);
}
