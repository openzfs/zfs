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
 * Copyright (c) 2012, 2020 by Delphix. All rights reserved.
 * Copyright 2017 RackTop Systems.
 * Copyright (c) 2017 Open-E, Inc. All Rights Reserved.
 * Copyright (c) 2019 Datto Inc.
 */

#ifndef	_LIBZFS_CORE_IMPL_H
#define	_LIBZFS_CORE_IMPL_H

struct zfs_cmd;
int lzc_ioctl_fd_os(int, unsigned long, struct zfs_cmd *);

#endif	/* _LIBZFS_CORE_IMPL_H */
