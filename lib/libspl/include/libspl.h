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
