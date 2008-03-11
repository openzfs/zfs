#include <sys/kobj.h>
#include "config.h"

void *rootdir = NULL;
EXPORT_SYMBOL(rootdir);

struct _buf *
kobj_open_file(const char *name)
{
	struct _buf *file;
	struct file *fp;

	fp = filp_open(name, O_RDONLY, 0644);
	if (IS_ERR(fp))
		return ((_buf_t *)-1UL);

	file = kmem_zalloc(sizeof(_buf_t), KM_SLEEP);
	file->fp = fp;

	return file;
} /* kobj_open_file() */
EXPORT_SYMBOL(kobj_open_file);

void
kobj_close_file(struct _buf *file)
{
        filp_close(file->fp, 0);
        kmem_free(file, sizeof(_buf_t));

	return;
} /* kobj_close_file() */
EXPORT_SYMBOL(kobj_close_file);

int
kobj_read_file(struct _buf *file, char *buf, unsigned size, unsigned off)
{
	loff_t offset = off;
	mm_segment_t saved_fs;
	int rc;

	if (!file || !file->fp)
		return -EINVAL;

	if (!file->fp->f_op || !file->fp->f_op->read)
		return -ENOSYS;

	/* Writable user data segment must be briefly increased for this
	 * process so we can use the user space read call paths to write
	 * in to memory allocated by the kernel. */
	saved_fs = get_fs();
        set_fs(get_ds());
	rc = file->fp->f_op->read(file->fp, buf, size, &offset);
	set_fs(saved_fs);

	return rc;
} /* kobj_read_file() */
EXPORT_SYMBOL(kobj_read_file);

int
kobj_get_filesize(struct _buf *file, uint64_t *size)
{
        struct kstat stat;
	int rc;

	if (!file || !file->fp || !size)
		return -EINVAL;

        rc = vfs_getattr(file->fp->f_vfsmnt, file->fp->f_dentry, &stat);
	if (rc)
		return rc;

        *size = stat.size;
        return rc;
} /* kobj_get_filesize() */
EXPORT_SYMBOL(kobj_get_filesize);
