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
 *
 * Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
 *
 */

#include <spl-debug.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <sys/rwlock.h>
#include <sys/kmem.h>

/*
 * In Unix, this lock is to protect the list of
 * mounted file-systems, to add and remove mounts.
 * XNU uses mount_lock() to hold all, then calls
 * lck_rw_lock_shared(mount_t) to hold this specific
 * mount - in future we can make this enhancement.
 */
static krwlock_t vfs_main_lock;

// Linked list of all mount
static list_t mount_list;
static kmutex_t mount_list_lock;

int
spl_vfs_init(void)
{
	rw_init(&vfs_main_lock, NULL, RW_DEFAULT, NULL);

	mutex_init(&mount_list_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&mount_list, sizeof (mount_t), offsetof(mount_t,
	    mount_node));

	return (0);
}

void
spl_vfs_fini(void)
{
	mutex_enter(&mount_list_lock);
	ASSERT(list_empty(&mount_list));
	list_destroy(&mount_list);
	mutex_exit(&mount_list_lock);

	mutex_destroy(&mount_list_lock);

	rw_destroy(&vfs_main_lock);
}

void
vfs_mount_add(mount_t *mp)
{
	mutex_enter(&mount_list_lock);
	// list_insert_head(&mount_list, mp);
	list_insert_tail(&mount_list, mp);
	mutex_exit(&mount_list_lock);
}

void
vfs_mount_remove(mount_t *mp)
{
	mutex_enter(&mount_list_lock);
	list_remove(&mount_list, mp);
	mutex_exit(&mount_list_lock);
}

int
vfs_mount_count()
{
	int count = 0;
	mount_t *node;

	mutex_enter(&mount_list_lock);
	for (node = list_head(&mount_list);
	    node;
	    node = list_next(&mount_list, node))
		count++;
	mutex_exit(&mount_list_lock);
	return (count);
}

boolean_t
vfs_mount_member(void *member)
{
	int count = 0;
	mount_t *node;

	mutex_enter(&mount_list_lock);
	for (node = list_head(&mount_list);
	    node;
	    node = list_next(&mount_list, node)) {
		if (node == member)
			break;
	}
	mutex_exit(&mount_list_lock);

	return (node == NULL ? B_FALSE : B_TRUE);
}

void
vfs_mount_setarray(void **array, int max)
{
	int count = 0;
	mount_t *node;

	mutex_enter(&mount_list_lock);
	for (node = list_head(&mount_list);
	    node;
	    node = list_next(&mount_list, node)) {
		array[count] = node;
		count++;
		if (count >= max)
			break;
	}
	mutex_exit(&mount_list_lock);
}

void
vfs_mount_iterate(int (*func)(void *, void *), void *priv)
{
	mount_t *node;

	mutex_enter(&mount_list_lock);
	for (node = list_head(&mount_list);
	    node;
	    node = list_next(&mount_list, node)) {

		// call func, stop if not zero
		if (func(node, priv) != 0)
			break;
	}
	mutex_exit(&mount_list_lock);
}

int
vfs_busy(mount_t *mp, int flags)
{
	BOOLEAN held = TRUE;
	krw_t rw = RW_READER;

	if (mp == NULL)
		return (EINVAL);

	if (flags & LK_UPGRADE) {
		// rwlock has no upgrade yet, drop READER
		rw_exit(&vfs_main_lock);
		rw = RW_WRITER;
	}

	if (flags & LK_NOWAIT) {
		held = rw_tryenter(&vfs_main_lock, rw);
	} else {
		rw_enter(&vfs_main_lock, rw);
	}

	if (!held)
		return (ESRCH);
	return (0);
}

void
vfs_unbusy(mount_t *mp)
{
	rw_exit(&vfs_main_lock);
}

int
vfs_main_lock_write_held(void)
{
	return (rw_write_held(&vfs_main_lock));
}

int
vfs_isrdonly(mount_t *mp)
{
	return (mp->mountflags & MNT_RDONLY);
}

void
vfs_setrdonly(mount_t *mp)
{
	mp->mountflags |= MNT_RDONLY;
}

void
vfs_clearrdonly(mount_t *mp)
{
	mp->mountflags &= ~MNT_RDONLY;
}

void *
vfs_fsprivate(mount_t *mp)
{
	return (mp->fsprivate);
}

void
vfs_setfsprivate(mount_t *mp, void *mntdata)
{
	mp->fsprivate = mntdata;
}

void
vfs_clearflags(mount_t *mp, uint64_t flags)
{
	mp->mountflags &= ~flags;
}

void
vfs_setflags(mount_t *mp, uint64_t flags)
{
	mp->mountflags |= flags;
}

uint64_t
vfs_flags(mount_t *mp)
{
	return (mp->mountflags);
}

struct vfsstatfs *
vfs_statfs(mount_t *mp)
{
	return (NULL);
}

void
vfs_setlocklocal(mount_t *mp)
{
}

int
vfs_typenum(mount_t *mp)
{
	return (0);
}

void
vfs_getnewfsid(struct mount *mp)
{
}

int
vfs_isunmount(mount_t *mp)
{
	return (vfs_flags(mp) & MNT_UNMOUNTING);
}

int
vfs_iswriteupgrade(mount_t *mp) /* ronly &&  MNTK_WANTRDWR */
{
	return (FALSE);
}

void
vfs_setextendedsecurity(mount_t *mp)
{
}

/*
 * Turns out we don't appear to need this, but since
 * we have it, it could be useful in future.
 */
void
vfs_set_mountedon(mount_t *mp, char *rootpath)
{
	VERIFY3P(mp, !=, NULL);

	if (rootpath == NULL || *rootpath == 0) {
		if (mp->mounted_on != NULL)
			kmem_strfree(mp->mounted_on);
		mp->mounted_on = NULL;
	} else {
		mp->mounted_on = kmem_strdup(rootpath);
	}
}

const char *
vfs_mountedon(mount_t *mp)
{
	return (mp->mounted_on ? mp->mounted_on : "\\");
}

mount_t *
vfs_has_mount(const char *rpath)
{
	int count = 0;
	mount_t *node;

	mutex_enter(&mount_list_lock);
	for (node = list_head(&mount_list);
	    node;
	    node = list_next(&mount_list, node)) {
		if (rpath && node->mounted_on &&
		    strcmp(rpath, node->mounted_on) == 0) {
			mutex_exit(&mount_list_lock);
			return (node);
		}
	}
	mutex_exit(&mount_list_lock);
	return (NULL);
}
