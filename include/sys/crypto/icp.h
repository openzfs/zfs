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
 * Copyright (c) 2016, Datto, Inc. All rights reserved.
 */

#ifndef	_SYS_CRYPTO_ALGS_H
#define	_SYS_CRYPTO_ALGS_H

int aes_mod_init(void);
int aes_mod_fini(void);

int sha2_mod_init(void);
int sha2_mod_fini(void);

int icp_init(void);
void icp_fini(void);

int aes_impl_set(const char *);
int gcm_impl_set(const char *);

#endif /* _SYS_CRYPTO_ALGS_H */
