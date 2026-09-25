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
 *
 * Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
 *
 */

#include <sys/processor.h>
#include <sys/simd.h>

/* Holds the flags for KeSaveExtendedProcessorState() in simd.h */
uint32_t kfpu_state = 0;

uint32_t
cpu_number(void)
{
	uint32_t cpuid;
	cpuid = (uint32_t)KeGetCurrentProcessorIndex();
	return (cpuid % max_ncpus);
}

uint32_t
getcpuid()
{
	uint32_t cpuid;
	cpuid = (uint32_t)KeGetCurrentProcessorIndex();
	return (cpuid % max_ncpus);
}

int
spl_processor_init(void)
{
#if defined(__x86_64__)
	dprintf("CPUID: %s%s%s%s%s%s%s\n",
	    zfs_osxsave_available() ? "osxsave " : "",
	    zfs_sse_available() ? "sse " : "",
	    zfs_sse2_available() ? "sse2 " : "",
	    zfs_sse3_available() ? "sse3 " : "",
	    zfs_ssse3_available() ? "ssse3 " : "",
	    zfs_sse4_1_available() ? "sse4.1 " : "",
	    zfs_sse4_2_available() ? "sse4.2 " : "");
	dprintf("CPUID: %s%s%s%s%s%s%s\n",
	    zfs_avx_available() ? "avx " : "",
	    zfs_avx2_available() ? "avx2 " : "",
	    zfs_aes_available() ? "aes " : "",
	    zfs_pclmulqdq_available() ? "pclmulqdq " : "",
	    zfs_avx512f_available() ? "avx512f " : "",
	    zfs_movbe_available() ? "movbe " : "",
	    zfs_shani_available() ? "sha-ni " : "");
#endif

	return (0);
}
