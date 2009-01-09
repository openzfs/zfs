#ifndef _SPL_ZFS_H
#define _SPL_ZFS_H

typedef struct vfs {
	int foo;
} vfs_t;

#define MAXFIDSZ	64

typedef struct fid {
	union {
		long fid_pad;
		struct {
			ushort_t len;		/* length of data in bytes */
			char     data[MAXFIDSZ];/* data (variable len) */
		} _fid;
	} un;
} fid_t;

#endif /* SPL_ZFS_H */
