
#ifndef _SPL_FILE_H
#define _SPL_FILE_H

#define	FIGNORECASE	0x00080000
#define	FKIOCTL		0x80000000
#define	FCOPYSTR	0x40000000

#include <sys/list.h>

struct spl_fileproc {
	void        *f_vnode;  // this points to the "fd" so we can look it up.
	list_node_t  f_next;   /* next zfsdev_state_t link */
	uint64_t     f_fd;
	uint64_t     f_offset;
	void        *f_proc;
	void        *f_fp;
	int          f_writes;
	uint64_t     f_file; // Minor of the file
	HANDLE	    f_handle;
	void	    *f_fileobject;
	void	    *f_deviceobject;
};

//typedef struct spl_fileproc file_t;
#define file_t struct spl_fileproc

void *getf(uint64_t fd);
void releasef(uint64_t fd);
/* O3X extended - get vnode from previos getf() */
struct vnode *getf_vnode(void *fp);

#endif /* SPL_FILE_H */
