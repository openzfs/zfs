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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_GCM_IMPL_H
#define	_GCM_IMPL_H

/*
 * GCM function dispatcher.
 */

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/zfs_context.h>
#include <sys/crypto/common.h>

/*
 * Methods used to define GCM implementation
 *
 * @gcm_mul_f Perform carry-less multiplication
 * @gcm_will_work_f Function tests whether implementation will function
 */
typedef void		(*gcm_mul_f)(uint64_t *, uint64_t *, uint64_t *);
typedef boolean_t	(*gcm_will_work_f)(void);

#define	GCM_IMPL_NAME_MAX (16)

typedef struct gcm_impl_ops {
	gcm_mul_f mul;
	gcm_will_work_f is_supported;
	char name[GCM_IMPL_NAME_MAX];
} gcm_impl_ops_t;

extern const gcm_impl_ops_t gcm_generic_impl;
#if defined(__x86_64) && HAVE_SIMD(PCLMULQDQ)
extern const gcm_impl_ops_t gcm_pclmulqdq_impl;
#endif

/*
 * Initializes fastest implementation
 */
void gcm_impl_init(void);

/*
 * Returns optimal allowed GCM implementation
 */
const struct gcm_impl_ops *gcm_impl_get_ops(void);

#ifdef	__cplusplus
}
#endif

#endif	/* _GCM_IMPL_H */
