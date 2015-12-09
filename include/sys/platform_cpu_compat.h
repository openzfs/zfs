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
 * Copyright (C) 2015 Intel Corporation.
 */

#ifndef _ZFS_CPU_COMPAT_H
#define	_ZFS_CPU_COMPAT_H

#include <sys/disp.h>

#ifdef __linux__
#include <asm/cpufeature.h>

#ifdef cpu_has_avx2
#include <asm/i387.h>

#define	HAVE_KERNEL_CPU_AVX2    1
#endif /* cpu_has_avx2 */


#define	kernel_cpu_relax()	cpu_relax()
#define	kernel_fpu_save()	kernel_fpu_begin()
#define	kernel_fpu_restore()	kernel_fpu_end()
#endif /* __linux__ */

#endif /* _ZFS_CPU_COMPAT_H */
