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

typedef struct vdev_disk {
	ddi_devid_t	vd_devid;
	char		*vd_minor;
	ldi_handle_t	vd_lh;
} vdev_disk_t;

extern int vdev_disk_physio(ldi_handle_t, caddr_t, size_t, uint64_t, int);
extern int vdev_disk_read_rootlabel(char *, char *, nvlist_t **);
#endif /* _KERNEL */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VDEV_DISK_H */
