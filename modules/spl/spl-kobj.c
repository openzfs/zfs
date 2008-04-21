#include <sys/kobj.h>
#include "config.h"

#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_KOBJ

struct _buf *
kobj_open_file(const char *name)
{
	struct _buf *file;
	vnode_t *vp;
	int rc;
	ENTRY;

	if ((rc = vn_open(name, UIO_SYSSPACE, FREAD, 0644, &vp, 0, 0)))
		RETURN((_buf_t *)-1UL);

	file = kmalloc(sizeof(_buf_t), GFP_KERNEL);
	file->vp = vp;

	RETURN(file);
} /* kobj_open_file() */
EXPORT_SYMBOL(kobj_open_file);

void
kobj_close_file(struct _buf *file)
{
	ENTRY;
	VOP_CLOSE(file->vp, 0, 0, 0, 0, 0);
	VN_RELE(file->vp);
        kfree(file);
        EXIT;
} /* kobj_close_file() */
EXPORT_SYMBOL(kobj_close_file);

int
kobj_read_file(struct _buf *file, char *buf, ssize_t size, offset_t off)
{
	ENTRY;
	RETURN(vn_rdwr(UIO_READ, file->vp, buf, size, off,
	       UIO_SYSSPACE, 0, RLIM64_INFINITY, 0, NULL));
} /* kobj_read_file() */
EXPORT_SYMBOL(kobj_read_file);

int
kobj_get_filesize(struct _buf *file, uint64_t *size)
{
        vattr_t vap;
	int rc;
	ENTRY;

	rc = VOP_GETATTR(file->vp, &vap, 0, 0, NULL);
	if (rc)
		RETURN(rc);

        *size = vap.va_size;

        RETURN(rc);
} /* kobj_get_filesize() */
EXPORT_SYMBOL(kobj_get_filesize);
