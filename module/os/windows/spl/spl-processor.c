// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
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
