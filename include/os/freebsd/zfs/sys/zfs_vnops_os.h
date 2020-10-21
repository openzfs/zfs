/*
 * Copyright (c) 2020 iXsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_SYS_FS_ZFS_VNOPS_OS_H
#define	_SYS_FS_ZFS_VNOPS_OS_H

int dmu_write_pages(objset_t *os, uint64_t object, uint64_t offset,
    uint64_t size, struct vm_page **ppa, dmu_tx_t *tx);
int dmu_read_pages(objset_t *os, uint64_t object, vm_page_t *ma, int count,
    int *rbehind, int *rahead, int last_size);
extern int zfs_remove(znode_t *dzp, const char *name, cred_t *cr, int flags);
extern int zfs_mkdir(znode_t *dzp, const char *dirname, vattr_t *vap,
    znode_t **zpp, cred_t *cr, int flags, vsecattr_t *vsecp);
extern int zfs_rmdir(znode_t *dzp, const char *name, znode_t *cwd,
    cred_t *cr, int flags);
extern int zfs_setattr(znode_t *zp, vattr_t *vap, int flag, cred_t *cr);
extern int zfs_rename(znode_t *sdzp, const char *snm, znode_t *tdzp,
    const char *tnm, cred_t *cr, int flags);
extern int zfs_symlink(znode_t *dzp, const char *name, vattr_t *vap,
    const char *link, znode_t **zpp, cred_t *cr, int flags);
extern int zfs_link(znode_t *tdzp, znode_t *sp,
    const char *name, cred_t *cr, int flags);
extern int zfs_space(znode_t *zp, int cmd, struct flock *bfp, int flag,
    offset_t offset, cred_t *cr);
extern int zfs_create(znode_t *dzp, const char *name, vattr_t *vap, int excl,
    int mode, znode_t **zpp, cred_t *cr, int flag, vsecattr_t *vsecp);
extern int zfs_setsecattr(znode_t *zp, vsecattr_t *vsecp, int flag,
    cred_t *cr);
extern int zfs_write_simple(znode_t *zp, const void *data, size_t len,
    loff_t pos, size_t *resid);

#endif
