#ifndef _SYS_VDEV_DISK_H
#define _SYS_VDEV_DISK_H

#ifdef	__cplusplus
extern "C" {
#endif

#ifdef _KERNEL
#include <sys/vdev.h>
#include <sys/ddi.h>
#include <sys/sunldi.h>
#include <sys/sunddi.h>
#include <zfs_config.h>

typedef struct vdev_disk {
	ddi_devid_t		vd_devid;
	char			*vd_minor;
	struct block_device	*vd_bdev;
} vdev_disk_t;

extern int vdev_disk_physio(struct block_device *, caddr_t,
			    size_t, uint64_t, int);
extern int vdev_disk_read_rootlabel(char *, char *, nvlist_t **);

/* 2.6.24 API change */
#ifdef HAVE_2ARGS_BIO_END_IO_T
# define BIO_END_IO_PROTO(fn, x, y, z)	static void fn(struct bio *x, int z)
# define BIO_END_IO_RETURN(rc)		return
#else
# define BIO_END_IO_PROTO(fn, x, y, z)	static int fn(struct bio *x, \
					              unsigned int y, int z)
# define BIO_END_IO_RETURN(rc)		return rc
#endif /* HAVE_2ARGS_BIO_END_IO_T */

/* 2.6.29 API change */
#ifdef HAVE_BIO_RW_SYNCIO
# define DIO_RW_SYNCIO			BIO_RW_SYNCIO
#else
# define DIO_RW_SYNCIO			BIO_RW_SYNC
#endif /* HAVE_BIO_RW_SYNCIO */

/* 2.6.28 API change */
#ifdef HAVE_OPEN_BDEV_EXCLUSIVE
# define vdev_bdev_open(path, md, hld)	open_bdev_exclusive(path, md, hld)
# define vdev_bdev_close(bdev, md)	close_bdev_exclusive(bdev, md)
#else
# define vdev_bdev_open(path, md, hld)	open_bdev_excl(path, md, hld)
# define vdev_bdev_close(bdev, md)	close_bdev_excl(bdev)
#endif /* HAVE_OPEN_BDEV_EXCLUSIVE */

/* 2.6.22 API change */
#ifdef HAVE_1ARG_INVALIDATE_BDEV
# define vdev_bdev_invalidate(bdev)	invalidate_bdev(bdev)
#else
# define vdev_bdev_invalidate(bdev)	invalidate_bdev(bdev, 1)
#endif /* HAVE_1ARG_INVALIDATE_BDEV */

/* 2.6.30 API change */
#ifdef HAVE_BDEV_LOGICAL_BLOCK_SIZE
# define vdev_bdev_block_size(bdev)	bdev_logical_block_size(bdev)
#else
# define vdev_bdev_block_size(bdev)	bdev_hardsect_size(bdev)
#endif

#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VDEV_DISK_H */
