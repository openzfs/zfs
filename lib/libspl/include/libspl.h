// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
 */

#ifndef _LIBSPL_H
#define	_LIBSPL_H extern __attribute__((visibility("default")))

#ifdef __cplusplus
extern "C" {
#endif

_LIBSPL_H void libspl_init(void);
_LIBSPL_H void libspl_fini(void);

#ifdef __cplusplus
};
#endif

#endif /* _LIBSPL_H */
