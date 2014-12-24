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

#if defined(_KERNEL) && defined(HAVE_DECLARE_EVENT_CLASS)

#undef TRACE_SYSTEM
#define	TRACE_SYSTEM zfs

#if !defined(_TRACE_ACL_H) || defined(TRACE_HEADER_MULTI_READ)
#define	_TRACE_ACL_H

#include <linux/tracepoint.h>
#include <sys/types.h>

/*
 * Generic support for three argument tracepoints of the form:
 *
 * DTRACE_PROBE3(...,
 *     znode_t *, ...,
 *     zfs_ace_hdr_t *, ...,
 *     uint32_t, ...);
 */

DECLARE_EVENT_CLASS(zfs_ace_class,
	TP_PROTO(znode_t *zn, zfs_ace_hdr_t *ace, uint32_t mask_matched),
	TP_ARGS(zn, ace, mask_matched),
	TP_STRUCT__entry(
	    __field(uint64_t,		z_id)
	    __field(uint8_t,		z_unlinked)
	    __field(uint8_t,		z_atime_dirty)
	    __field(uint8_t,		z_zn_prefetch)
	    __field(uint8_t,		z_moved)
	    __field(uint_t,		z_blksz)
	    __field(uint_t,		z_seq)
	    __field(uint64_t,		z_mapcnt)
	    __field(uint64_t,		z_gen)
	    __field(uint64_t,		z_size)
	    __array(uint64_t,		z_atime, 2)
	    __field(uint64_t,		z_links)
	    __field(uint64_t,		z_pflags)
	    __field(uint64_t,		z_uid)
	    __field(uint64_t,		z_gid)
	    __field(uint32_t,		z_sync_cnt)
	    __field(mode_t,		z_mode)
	    __field(boolean_t,		z_is_sa)
	    __field(boolean_t,		z_is_zvol)
	    __field(boolean_t,		z_is_mapped)
	    __field(boolean_t,		z_is_ctldir)
	    __field(boolean_t,		z_is_stale)

	    __field(unsigned long,	i_ino)
	    __field(unsigned int,	i_nlink)
	    __field(u64,		i_version)
	    __field(loff_t,		i_size)
	    __field(unsigned int,	i_blkbits)
	    __field(unsigned short,	i_bytes)
	    __field(umode_t,		i_mode)
	    __field(__u32,		i_generation)

	    __field(uint16_t,		z_type)
	    __field(uint16_t,		z_flags)
	    __field(uint32_t,		z_access_mask)

	    __field(uint32_t,		mask_matched)
	),
	TP_fast_assign(
	    __entry->z_id		= zn->z_id;
	    __entry->z_unlinked		= zn->z_unlinked;
	    __entry->z_atime_dirty	= zn->z_atime_dirty;
	    __entry->z_zn_prefetch	= zn->z_zn_prefetch;
	    __entry->z_moved		= zn->z_moved;
	    __entry->z_blksz		= zn->z_blksz;
	    __entry->z_seq		= zn->z_seq;
	    __entry->z_mapcnt		= zn->z_mapcnt;
	    __entry->z_gen		= zn->z_gen;
	    __entry->z_size		= zn->z_size;
	    __entry->z_atime[0]		= zn->z_atime[0];
	    __entry->z_atime[1]		= zn->z_atime[1];
	    __entry->z_links		= zn->z_links;
	    __entry->z_pflags		= zn->z_pflags;
	    __entry->z_uid		= zn->z_uid;
	    __entry->z_gid		= zn->z_gid;
	    __entry->z_sync_cnt		= zn->z_sync_cnt;
	    __entry->z_mode		= zn->z_mode;
	    __entry->z_is_sa		= zn->z_is_sa;
	    __entry->z_is_zvol		= zn->z_is_zvol;
	    __entry->z_is_mapped	= zn->z_is_mapped;
	    __entry->z_is_ctldir	= zn->z_is_ctldir;
	    __entry->z_is_stale		= zn->z_is_stale;

	    __entry->i_ino		= zn->z_inode.i_ino;
	    __entry->i_nlink		= zn->z_inode.i_nlink;
	    __entry->i_version		= zn->z_inode.i_version;
	    __entry->i_size		= zn->z_inode.i_size;
	    __entry->i_blkbits		= zn->z_inode.i_blkbits;
	    __entry->i_bytes		= zn->z_inode.i_bytes;
	    __entry->i_mode		= zn->z_inode.i_mode;
	    __entry->i_generation	= zn->z_inode.i_generation;

	    __entry->z_type		= ace->z_type;
	    __entry->z_flags		= ace->z_flags;
	    __entry->z_access_mask	= ace->z_access_mask;

	    __entry->mask_matched	= mask_matched;
	),
	TP_printk("zn { id %llu unlinked %u atime_dirty %u "
	    "zn_prefetch %u moved %u blksz %u seq %u "
	    "mapcnt %llu gen %llu size %llu atime 0x%llx:0x%llx "
	    "links %llu pflags %llu uid %llu gid %llu "
	    "sync_cnt %u mode 0x%x is_sa %d is_zvol %d "
	    "is_mapped %d is_ctldir %d is_stale %d inode { "
	    "ino %lu nlink %u version %llu size %lli blkbits %u "
	    "bytes %u mode 0x%x generation %x } } ace { type %u "
	    "flags %u access_mask %u } mask_matched %u",
	    __entry->z_id, __entry->z_unlinked, __entry->z_atime_dirty,
	    __entry->z_zn_prefetch, __entry->z_moved, __entry->z_blksz,
	    __entry->z_seq, __entry->z_mapcnt, __entry->z_gen,
	    __entry->z_size, __entry->z_atime[0], __entry->z_atime[1],
	    __entry->z_links, __entry->z_pflags, __entry->z_uid,
	    __entry->z_gid, __entry->z_sync_cnt, __entry->z_mode,
	    __entry->z_is_sa, __entry->z_is_zvol, __entry->z_is_mapped,
	    __entry->z_is_ctldir, __entry->z_is_stale, __entry->i_ino,
	    __entry->i_nlink, __entry->i_version, __entry->i_size,
	    __entry->i_blkbits, __entry->i_bytes, __entry->i_mode,
	    __entry->i_generation, __entry->z_type, __entry->z_flags,
	    __entry->z_access_mask, __entry->mask_matched)
);

#define	DEFINE_ACE_EVENT(name) \
DEFINE_EVENT(zfs_ace_class, name, \
	TP_PROTO(znode_t *zn, zfs_ace_hdr_t *ace, uint32_t mask_matched), \
	TP_ARGS(zn, ace, mask_matched))
DEFINE_ACE_EVENT(zfs_zfs__ace__denies);
DEFINE_ACE_EVENT(zfs_zfs__ace__allows);

#endif /* _TRACE_ACL_H */

#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define	TRACE_INCLUDE_PATH sys
#define	TRACE_INCLUDE_FILE trace_acl
#include <trace/define_trace.h>

#endif /* _KERNEL && HAVE_DECLARE_EVENT_CLASS */
