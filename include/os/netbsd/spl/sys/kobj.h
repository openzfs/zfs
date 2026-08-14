// SPDX-License-Identifier: BSD-2-Clause
/*
 * osnet kobj file helpers.  FBT's kobj_find_section lives in the
 * NetBSD kernel (ksyms); this is the solaris.kmod side that reads
 * files for firmware / CTF blobs.
 */

#ifndef _OPENSOLARIS_SYS_KOBJ_H_
#define	_OPENSOLARIS_SYS_KOBJ_H_

#ifdef _KERNEL

#include <sys/types.h>

struct _buf {
	struct vnode	*vp;
	off_t		off;

};

void	*kobj_alloc(size_t, int);
void	*kobj_zalloc(size_t, int);
void	kobj_free(void *, size_t);

struct _buf	*kobj_open_file(const char *);
void		kobj_close_file(struct _buf *);
int		kobj_read_file(struct _buf *, char *, unsigned, unsigned);
int		kobj_get_filesize(struct _buf *, uint64_t *);

#endif /* _KERNEL */

#endif /* _OPENSOLARIS_SYS_KOBJ_H_ */
