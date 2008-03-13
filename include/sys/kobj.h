#ifndef _SPL_KOBJ_H
#define _SPL_KOBJ_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <sys/vnode.h>

typedef struct _buf {
	vnode_t *vp;
} _buf_t;

typedef struct _buf buf_t;

extern struct _buf *kobj_open_file(const char *name);
extern void kobj_close_file(struct _buf *file);
extern int kobj_read_file(struct _buf *file, char *buf,
			  ssize_t size, offset_t off);
extern int kobj_get_filesize(struct _buf *file, uint64_t *size);

#ifdef  __cplusplus
}
#endif

#endif /* SPL_KOBJ_H */
