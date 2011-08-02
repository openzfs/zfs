/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
 * Copyright (C) 2011 Lawrence Livermore National Security, LLC.
 */

#ifndef _ZFS_VFS_H
#define _ZFS_VFS_H

/*
 * 2.6.28 API change,
 * Added insert_inode_locked() helper function, prior to this most callers
 * used insert_inode_hash().  The older method doesn't check for collisions
 * in the inode_hashtable but it still acceptible for use.
 */
#ifndef HAVE_INSERT_INODE_LOCKED
static inline int
insert_inode_locked(struct inode *ip)
{
	insert_inode_hash(ip);
	return (0);
}
#endif /* HAVE_INSERT_INODE_LOCKED */

/*
 * 2.6.35 API change,
 * Add truncate_setsize() if it is not exported by the Linux kernel.
 *
 * Truncate the inode and pages associated with the inode. The pages are
 * unmapped and removed from cache.
 */
#ifndef HAVE_TRUNCATE_SETSIZE
static inline void
truncate_setsize(struct inode *ip, loff_t new)
{
	struct address_space *mapping = ip->i_mapping;

	i_size_write(ip, new);

	unmap_mapping_range(mapping, new + PAGE_SIZE - 1, 0, 1);
	truncate_inode_pages(mapping, new);
	unmap_mapping_range(mapping, new + PAGE_SIZE - 1, 0, 1);
}
#endif /* HAVE_TRUNCATE_SETSIZE */

/*
 * 2.6.32 API change,
 * Added backing_device_info (bdi) per super block interfaces.  When
 * available a bdi must be configured when using a non-device backed
 * filesystem for proper writeback.  It's safe to leave this code
 * dormant for kernels which only support pdflush and not bdi.
 */
#ifdef HAVE_BDI
#define	bdi_get_sb(sb)				(sb->s_bdi)
#define	bdi_put_sb(sb, bdi)			(sb->s_bdi = bdi)
#else
#define	bdi_init(bdi)				(0)
#define	bdi_destroy(bdi)			(0)
#define	bdi_register(bdi, parent, fmt, args)	(0)
#define	bdi_unregister(bdi)			(0)
#define	bdi_get_sb(sb)				(0)
#define	bdi_put_sb(sb, bdi)			(0)
#endif /* HAVE_BDI */

#endif /* _ZFS_VFS_H */
