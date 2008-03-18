#ifndef _SPL_FILE_H
#define _SPL_FILE_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <linux/module.h>
#include <linux/file.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/vnode.h>

typedef struct spl_file {
	int			f_fd;	   /* linux fd for lookup */
	struct file		*f_file;   /* linux file struct */
	atomic_t		f_ref;	   /* ref count */
	kmutex_t		f_lock;    /* struct lock */
	loff_t			f_offset;  /* offset */
	vnode_t			*f_vnode;  /* vnode */
        struct list_head	f_list;	   /* list of referenced file_t's */
} file_t;

extern file_t *getf(int fd);
extern void releasef(int fd);

int file_init(void);
void file_fini(void);

#ifdef  __cplusplus
}
#endif

#endif /* SPL_FILE_H */
