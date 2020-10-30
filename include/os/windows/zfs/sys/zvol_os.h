/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
#ifndef _SYS_ZVOL_OS_h
#define	_SYS_ZVOL_OS_h

#ifdef  __cplusplus
extern "C" {
#endif

/* struct wrapper for IOKit class */
typedef struct zvol_iokit zvol_iokit_t;
typedef struct zvol_state zvol_state_t;
struct iomem;

#define	ZVOL_WRITE_SYNC 0x10

struct zvol_state_os {
	dev_t		zvo_dev;	/* device id */

	uint64_t	zvo_openflags;	/* Remember flags used at open */
	uint8_t		zv_target_id;
	uint8_t		zv_lun_id;
	void		*zv_target_context; // for I/O drainage (see assign_targetid, clear_targetid)
};

extern int zvol_os_ioctl(dev_t, unsigned long, caddr_t,
    int isblk, cred_t *, int *rvalp);
extern int zvol_os_open_zv(zvol_state_t *, int, int, struct proc *p);
extern int zvol_os_open(dev_t dev, int flag, int otyp, struct proc *p);
extern int zvol_os_close_zv(zvol_state_t *, int, int, struct proc *p);
extern int zvol_os_close(dev_t dev, int flag, int otyp, struct proc *p);
extern int zvol_os_read(dev_t dev, struct uio *uio, int p);
extern int zvol_os_write(dev_t dev, struct uio *uio, int p);

extern int zvol_os_read_zv(zvol_state_t *zv, uio_t *, int);
extern int zvol_os_write_zv(zvol_state_t *zv, uio_t *, int);
extern int zvol_os_unmap(zvol_state_t *zv, uint64_t off, uint64_t bytes);

extern void zvol_os_strategy(struct buf *bp);
extern int zvol_os_get_volume_blocksize(dev_t dev);

extern void zvol_os_lock_zv(zvol_state_t *zv);
extern void zvol_os_unlock_zv(zvol_state_t *zv);

extern void *zvolRemoveDevice(zvol_iokit_t *iokitdev);
extern int zvolRemoveDeviceTerminate(void *iokitdev);
extern int zvolCreateNewDevice(zvol_state_t *zv);
extern int zvolRegisterDevice(zvol_state_t *zv);

extern int zvolRenameDevice(zvol_state_t *zv);
extern int zvolSetVolsize(zvol_state_t *zv);

#ifdef  __cplusplus
}
#endif

#endif
