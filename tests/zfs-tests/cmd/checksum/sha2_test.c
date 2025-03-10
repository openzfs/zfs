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
 * running in userspace, but we need -D_KERNEL for the remaining SHA2 code).
 */

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <sys/time.h>
#include <sys/sha2.h>
#include <sys/stdtypes.h>
#include <sys/zfs_impl.h>

/*
 * Test messages from:
 * http://csrc.nist.gov/groups/ST/toolkit/documents/Examples/SHA_All.pdf
 */

static const char *test_msg0 = "abc";
static const char *test_msg1 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklm"
	"nlmnomnopnopq";
static const char *test_msg2 = "abcdefghbcdefghicdefghijdefghijkefghijklfgh"
	"ijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

/*
 * Test digests from:
 * http://csrc.nist.gov/groups/ST/toolkit/documents/Examples/SHA_All.pdf
 */
static const uint8_t	sha256_test_digests[][32] = {
	{
		/* for test_msg0 */
		0xBA, 0x78, 0x16, 0xBF, 0x8F, 0x01, 0xCF, 0xEA,
		0x41, 0x41, 0x40, 0xDE, 0x5D, 0xAE, 0x22, 0x23,
		0xB0, 0x03, 0x61, 0xA3, 0x96, 0x17, 0x7A, 0x9C,
		0xB4, 0x10, 0xFF, 0x61, 0xF2, 0x00, 0x15, 0xAD
	},
	{
		/* for test_msg1 */
		0x24, 0x8D, 0x6A, 0x61, 0xD2, 0x06, 0x38, 0xB8,
		0xE5, 0xC0, 0x26, 0x93, 0x0C, 0x3E, 0x60, 0x39,
		0xA3, 0x3C, 0xE4, 0x59, 0x64, 0xFF, 0x21, 0x67,
		0xF6, 0xEC, 0xED, 0xD4, 0x19, 0xDB, 0x06, 0xC1
	}
	/* no test vector for test_msg2 */
};

static const uint8_t	sha512_test_digests[][64] = {
	{
		/* for test_msg0 */
		0xDD, 0xAF, 0x35, 0xA1, 0x93, 0x61, 0x7A, 0xBA,
		0xCC, 0x41, 0x73, 0x49, 0xAE, 0x20, 0x41, 0x31,
		0x12, 0xE6, 0xFA, 0x4E, 0x89, 0xA9, 0x7E, 0xA2,
		0x0A, 0x9E, 0xEE, 0xE6, 0x4B, 0x55, 0xD3, 0x9A,
		0x21, 0x92, 0x99, 0x2A, 0x27, 0x4F, 0xC1, 0xA8,
		0x36, 0xBA, 0x3C, 0x23, 0xA3, 0xFE, 0xEB, 0xBD,
		0x45, 0x4D, 0x44, 0x23, 0x64, 0x3C, 0xE8, 0x0E,
		0x2A, 0x9A, 0xC9, 0x4F, 0xA5, 0x4C, 0xA4, 0x9F
	},
	{
		/* no test vector for test_msg1 */
		0
	},
	{
		/* for test_msg2 */
		0x8E, 0x95, 0x9B, 0x75, 0xDA, 0xE3, 0x13, 0xDA,
		0x8C, 0xF4, 0xF7, 0x28, 0x14, 0xFC, 0x14, 0x3F,
		0x8F, 0x77, 0x79, 0xC6, 0xEB, 0x9F, 0x7F, 0xA1,
		0x72, 0x99, 0xAE, 0xAD, 0xB6, 0x88, 0x90, 0x18,
		0x50, 0x1D, 0x28, 0x9E, 0x49, 0x00, 0xF7, 0xE4,
		0x33, 0x1B, 0x99, 0xDE, 0xC4, 0xB5, 0x43, 0x3A,
		0xC7, 0xD3, 0x29, 0xEE, 0xB6, 0xDD, 0x26, 0x54,
		0x5E, 0x96, 0xE5, 0x5B, 0x87, 0x4B, 0xE9, 0x09
	}
};

static const uint8_t	sha512_256_test_digests[][32] = {
	{
		/* for test_msg0 */
		0x53, 0x04, 0x8E, 0x26, 0x81, 0x94, 0x1E, 0xF9,
		0x9B, 0x2E, 0x29, 0xB7, 0x6B, 0x4C, 0x7D, 0xAB,
		0xE4, 0xC2, 0xD0, 0xC6, 0x34, 0xFC, 0x6D, 0x46,
		0xE0, 0xE2, 0xF1, 0x31, 0x07, 0xE7, 0xAF, 0x23
	},
	{
		/* no test vector for test_msg1 */
		0
	},
	{
		/* for test_msg2 */
		0x39, 0x28, 0xE1, 0x84, 0xFB, 0x86, 0x90, 0xF8,
		0x40, 0xDA, 0x39, 0x88, 0x12, 0x1D, 0x31, 0xBE,
		0x65, 0xCB, 0x9D, 0x3E, 0xF8, 0x3E, 0xE6, 0x14,
		0x6F, 0xEA, 0xC8, 0x61, 0xE1, 0x9B, 0x56, 0x3A
	}
};

int
main(int argc, char *argv[])
{
	boolean_t	failed = B_FALSE;
	uint64_t	cpu_mhz = 0;

	const zfs_impl_t *sha256 = zfs_impl_get_ops("sha256");
	const zfs_impl_t *sha512 = zfs_impl_get_ops("sha512");
	uint32_t id;

	if (argc == 2)
		cpu_mhz = atoi(argv[1]);

	if (!sha256)
		return (1);

	if (!sha512)
		return (1);

#define	SHA2_ALGO_TEST(_m, mode, diglen, testdigest)			\
	do {								\
		SHA2_CTX		ctx;				\
		uint8_t			digest[diglen / 8];		\
		SHA2Init(mode, &ctx);					\
		SHA2Update(&ctx, _m, strlen(_m));			\
		SHA2Final(digest, &ctx);				\
		(void) printf("SHA%-9sMessage: " #_m			\
		    "\tResult: ", #mode);				\
		if (memcmp(digest, testdigest, diglen / 8) == 0) {	\
			(void) printf("OK\n");				\
		} else {						\
			(void) printf("FAILED!\n");			\
			failed = B_TRUE;				\
		}							\
	} while (0)

#define	SHA2_PERF_TEST(mode, diglen, name)				\
	do {								\
		SHA2_CTX	ctx;					\
		uint8_t		digest[diglen / 8];			\
		uint8_t		block[131072];				\
		uint64_t	delta;					\
		double		cpb = 0;				\
		int		i;					\
		struct timeval	start, end;				\
		memset(block, 0, sizeof (block));			\
		(void) gettimeofday(&start, NULL);			\
		SHA2Init(mode, &ctx);					\
		for (i = 0; i < 8192; i++)				\
			SHA2Update(&ctx, block, sizeof (block));	\
		SHA2Final(digest, &ctx);				\
		(void) gettimeofday(&end, NULL);			\
		delta = (end.tv_sec * 1000000llu + end.tv_usec) -	\
		    (start.tv_sec * 1000000llu + start.tv_usec);	\
		if (cpu_mhz != 0) {					\
			cpb = (cpu_mhz * 1e6 * ((double)delta /		\
			    1000000)) / (8192 * 128 * 1024);		\
		}							\
		(void) printf("sha%s-%-9s%7llu us (%.02f CPB)\n", #mode,\
		    name, (u_longlong_t)delta, cpb);			\
	} while (0)

	(void) printf("Running algorithm correctness tests:\n");
	SHA2_ALGO_TEST(test_msg0, SHA256, 256, sha256_test_digests[0]);
	SHA2_ALGO_TEST(test_msg1, SHA256, 256, sha256_test_digests[1]);
	SHA2_ALGO_TEST(test_msg0, SHA512, 512, sha512_test_digests[0]);
	SHA2_ALGO_TEST(test_msg2, SHA512, 512, sha512_test_digests[2]);
	SHA2_ALGO_TEST(test_msg0, SHA512_256, 256, sha512_256_test_digests[0]);
	SHA2_ALGO_TEST(test_msg2, SHA512_256, 256, sha512_256_test_digests[2]);

	if (failed)
		return (1);

	(void) printf("Running performance tests (hashing 1024 MiB of "
	    "data):\n");

	for (id = 0; id < sha256->getcnt(); id++) {
		sha256->setid(id);
		const char *name = sha256->getname();
		SHA2_PERF_TEST(SHA256, 256, name);
	}

	for (id = 0; id < sha512->getcnt(); id++) {
		sha512->setid(id);
		const char *name = sha512->getname();
		SHA2_PERF_TEST(SHA512, 512, name);
	}

	return (0);
}
