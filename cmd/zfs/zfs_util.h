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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#ifndef	_ZFS_UTIL_H
#define	_ZFS_UTIL_H

#include <libzfs.h>

#ifdef	__cplusplus
extern "C" {
#endif

void *safe_malloc(size_t size);
void nomem(void);
extern libzfs_handle_t *g_zfs;

#ifdef	__cplusplus
}
#endif

#endif	/* _ZFS_UTIL_H */
