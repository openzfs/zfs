// SPDX-License-Identifier: BSD-2-Clause
/*
 * osnet kobj file helpers on NetBSD vnode ops.
 * kobj_find_section stays in the kernel (ksyms); FBT uses that.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/namei.h>
#include <sys/kmem.h>
#include <sys/kobj.h>
#include <sys/fcntl.h>
#include <sys/kauth.h>
#include <sys/uio.h>

/*
 * vnode.h remaps vn_open / vn_rdwr to the Solaris-shaped wrappers.
 * kobj talks to the native VFS, same as zfs_file_os.c.
 */
#undef vn_open
#undef vn_rdwr
#undef vn_lock
#undef VOP_GETATTR
#undef VOP_UNLOCK


void *
kobj_alloc(size_t size, int flag)
{
	return (solaris_kmem_alloc(size,
	    (flag & KM_NOSLEEP) ? KM_NOSLEEP : KM_SLEEP));
}

void *
kobj_zalloc(size_t size, int flag)
{
	return (solaris_kmem_zalloc(size,
	    (flag & KM_NOSLEEP) ? KM_NOSLEEP : KM_SLEEP));
}

void
kobj_free(void *buf, size_t size)
{
	solaris_kmem_free(buf, size);
}

struct _buf *
kobj_open_file(const char *file)
{
	struct pathbuf *pb;
	struct vnode *vp;
	struct _buf *bp;
	int error;

	if (file == NULL)
		return ((struct _buf *)-1);
	pb = pathbuf_create(file);
	if (pb == NULL)
		return ((struct _buf *)-1);
	error = vn_open(NULL, pb, 0, FREAD, 0, &vp, NULL, NULL);
	pathbuf_destroy(pb);
	if (error != 0)
		return ((struct _buf *)-1);
	VOP_UNLOCK(vp);
	bp = solaris_kmem_zalloc(sizeof (*bp), KM_SLEEP);
	bp->vp = vp;
	bp->off = 0;
	return (bp);
}

void
kobj_close_file(struct _buf *bp)
{
	if (bp == NULL || bp == (struct _buf *)-1)
		return;
	if (bp->vp != NULL) {
		(void) vn_close(bp->vp, FREAD, FSCRED);
	}
	solaris_kmem_free(bp, sizeof (*bp));
}

int
kobj_read_file(struct _buf *bp, char *buf, unsigned size, unsigned off)
{
	int error;
	size_t resid;

	if (bp == NULL || bp == (struct _buf *)-1 || bp->vp == NULL)
		return (-1);
	error = vn_rdwr(UIO_READ, bp->vp, buf, (int)size, (off_t)off,
	    UIO_SYSSPACE, IO_UNIT, FSCRED, &resid, curlwp);
	if (error != 0)
		return (-1);
	return ((int)(size - resid));
}

int
kobj_get_filesize(struct _buf *bp, uint64_t *size)
{
	struct vattr va;
	int error;

	if (bp == NULL || bp == (struct _buf *)-1 || bp->vp == NULL)
		return (EINVAL);
	vn_lock(bp->vp, LK_SHARED | LK_RETRY);
	error = VOP_GETATTR(bp->vp, &va, FSCRED);
	VOP_UNLOCK(bp->vp);
	if (error != 0)
		return (error);
	*size = (uint64_t)va.va_size;
	return (0);
}
