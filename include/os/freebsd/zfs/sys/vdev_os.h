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

#ifndef _SYS_VDEV_OS_H
#define	_SYS_VDEV_OS_H

extern int vdev_label_write_pad2(vdev_t *vd, const char *buf, size_t size);
extern int vdev_geom_read_pool_label(const char *name, nvlist_t ***configs,
    uint64_t *count);

#endif
