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
 * In-kernel, use the kernel crypto API if the configure checks found it
 * usable and the kernel builds the AEAD and hash APIs (CRYPTO_AEAD2 and
 * CRYPTO_HASH2 are what compile aead.o and shash.o). The configure check
 * alone is not enough: when configuring for builtin (--enable-linux-builtin)
 * it only tests that the headers compile. Userspace (libzpool) always uses
 * the ICP backend.
 */
#if defined(_KERNEL) && defined(HAVE_KERNEL_CRYPTO)
#if IS_REACHABLE(CONFIG_CRYPTO_AEAD2) && IS_REACHABLE(CONFIG_CRYPTO_HASH2)
#define	ZIO_CRYPT_OS_KCAPI
#endif
#endif

#ifdef ZIO_CRYPT_OS_KCAPI
#include <sys/zio_crypt_os_kcapi.h>
#else
#include <sys/zio_crypt_os_icp.h>
#endif

#endif
