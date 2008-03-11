#ifndef _SPL_KOBJ_H
#define _SPL_KOBJ_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <linux/module.h>
#include <linux/uaccess.h>
#include <sys/types.h>
#include <sys/kmem.h>

typedef struct _buf {
	struct file *fp;
} _buf_t;

extern void *rootdir;

extern struct _buf *kobj_open_file(const char *name);
extern void kobj_close_file(struct _buf *file);
extern int kobj_read_file(struct _buf *file, char *buf,
			  unsigned size, unsigned off);
extern int kobj_get_filesize(struct _buf *file, uint64_t *size);

#ifdef  __cplusplus
}
#endif

#endif /* SPL_KOBJ_H */
