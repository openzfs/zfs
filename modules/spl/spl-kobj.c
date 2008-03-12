#include <sys/kobj.h>
#include "config.h"

struct _buf *
kobj_open_file(const char *name)
{
	struct _buf *file;
	vnode_t *vp;
	int rc;

	if ((rc = vn_open(name, UIO_SYSSPACE, FREAD, 0644, &vp, 0, 0)))
		return ((_buf_t *)-1UL);

	file = kmalloc(sizeof(_buf_t), GFP_KERNEL);
	file->vp = vp;

	return file;
} /* kobj_open_file() */
EXPORT_SYMBOL(kobj_open_file);

void
kobj_close_file(struct _buf *file)
{
	VOP_CLOSE(file->vp, 0, 0, 0, 0, 0);
	VN_RELE(file->vp);
        kfree(file);

	return;
} /* kobj_close_file() */
EXPORT_SYMBOL(kobj_close_file);

int
kobj_read_file(struct _buf *file, char *buf, ssize_t size, offset_t off)
{
	return vn_rdwr(UIO_READ, file->vp, buf, size, off,
		       UIO_SYSSPACE, 0, RLIM64_INFINITY, 0, NULL);
} /* kobj_read_file() */
EXPORT_SYMBOL(kobj_read_file);

int
kobj_get_filesize(struct _buf *file, uint64_t *size)
{
        vattr_t vap;
	int rc;

	rc = VOP_GETATTR(file->vp, &vap, 0, 0, NULL);
	if (rc)
		return rc;

        *size = vap.va_size;

        return rc;
} /* kobj_get_filesize() */
EXPORT_SYMBOL(kobj_get_filesize);
