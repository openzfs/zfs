// SPDX-License-Identifier: BSD-2-Clause
/*
 * dmu_buf_* aliases: NetBSD ld does not honour #pragma weak.
 * FreeBSD pager helpers (dmu_read_pages / dmu_write_pages) are not
 * used: NetBSD goes through genfs getpages/putpages.
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/dmu.h>
#include <sys/dmu_impl.h>
#include <sys/dbuf.h>
#include <sys/zfs_context.h>

void
dmu_buf_add_ref(dmu_buf_t *db, const void *tag)
{
	dbuf_add_ref((dmu_buf_impl_t *)db, tag);
}

boolean_t
dmu_buf_try_add_ref(dmu_buf_t *db, objset_t *os, uint64_t obj, uint64_t blkid,
    const void *tag)
{
	return (dbuf_try_add_ref(db, os, obj, blkid, tag));
}

uint64_t
dmu_buf_refcount(dmu_buf_t *db)
{
	return (dbuf_refcount((dmu_buf_impl_t *)db));
}
