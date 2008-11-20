/* -*- mode: c; c-basic-offset: 8; indent-tabs-mode: nil; -*-
 * vim:expandtab:shiftwidth=8:tabstop=8:
 *
 *  lustre/dmu/udmu.c
 *  Module that interacts with the ZFS DMU and provides an abstraction
 *  to the rest of Lustre.
 *
 *  Copyright (c) 2007 Cluster File Systems, Inc.
 *   Author: Manoj Joseph <manoj.joseph@sun.com>
 *
 *   This file is part of the Lustre file system, http://www.lustre.org
 *   Lustre is a trademark of Cluster File Systems, Inc.
 *
 *   You may have signed or agreed to another license before downloading
 *   this software.  If so, you are bound by the terms and conditions
 *   of that agreement, and the following does not apply to you.  See the
 *   LICENSE file included with this distribution for more information.
 *
 *   If you did not agree to a different license, then this copy of Lustre
 *   is open source software; you can redistribute it and/or modify it
 *   under the terms of version 2 of the GNU General Public License as
 *   published by the Free Software Foundation.
 *
 *   In either case, Lustre is distributed in the hope that it will be
 *   useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 *   of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   license text for more details.
 */

#ifndef _DMU_UTIL_H
#define _DMU_UTIL_H

#ifdef DMU_OSD

#ifdef __cplusplus
extern "C" {
#endif

int udmu_util_lookup(udmu_objset_t *uos, dmu_buf_t *parent_db,
                     const char *name, dmu_buf_t **new_dbp, void *tag);

int udmu_util_create(udmu_objset_t *uos, dmu_buf_t *parent_db,
                     const char *name, dmu_buf_t **new_db, void *tag);

int udmu_util_mkdir(udmu_objset_t *uos, dmu_buf_t *parent_db,
                    const char *name, dmu_buf_t **new_db, void *tag);

int udmu_util_setattr(udmu_objset_t *uos, dmu_buf_t *db, vnattr_t *va);

int udmu_util_write(udmu_objset_t *uos, dmu_buf_t *db,
                    uint64_t offset, uint64_t len, void *buf);

#ifdef __cplusplus
}
#endif

#endif /* DMU_OSD */

#endif /* _DMU_UTIL_H */
