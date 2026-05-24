// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2017, Datto, Inc. All rights reserved.
 * Copyright (c) 2026, TrueNAS.
 */

#ifndef	_SYS_ZIO_CRYPT_OS_ICP_H
#define	_SYS_ZIO_CRYPT_OS_ICP_H

#include <sys/crypto/api.h>

typedef struct zio_crypt_session {
	crypto_ctx_template_t	zs_tmpl;
} zio_crypt_session_t;

typedef struct zio_crypt_hmac {
	crypto_context_t	zh_ctx;
} zio_crypt_hmac_t;

#endif
