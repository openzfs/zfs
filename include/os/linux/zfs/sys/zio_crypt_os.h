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
 * Copyright (c) 2017, Datto, Inc. All rights reserved.
 * Copyright (c) 2026, TrueNAS.
 */

#ifndef	_SYS_ZIO_CRYPT_OS_H
#define	_SYS_ZIO_CRYPT_OS_H

/*
 * In-kernel, use the kernel crypto API backend when configure selected it
 * (see config/kernel-crypto.m4), otherwise the ICP backend. Userspace
 * (libzpool) always uses the ICP backend.
 */
#if defined(_KERNEL) && defined(HAVE_KERNEL_CRYPTO)
#include <sys/zio_crypt_os_kcapi.h>
#else
#include <sys/zio_crypt_os_icp.h>
#endif

#endif
