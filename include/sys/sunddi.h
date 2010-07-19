/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://github.com/behlendorf/spl/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

#ifndef _SPL_SUNDDI_H
#define _SPL_SUNDDI_H

#include <sys/cred.h>
#include <sys/uio.h>
#include <sys/sunldi.h>
#include <sys/mutex.h>
#include <sys/u8_textprep.h>
#include <sys/vnode.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <spl-device.h>

#define DDI_MAX_NAME_LEN	32

typedef int ddi_devid_t;

typedef enum {
	DDI_INFO_DEVT2DEVINFO	= 0,
	DDI_INFO_DEVT2INSTANCE	= 1
} ddi_info_cmd_t;

typedef enum {
	DDI_ATTACH		= 0,
	DDI_RESUME		= 1,
	DDI_PM_RESUME		= 2
} ddi_attach_cmd_t;

typedef enum {
	DDI_DETACH		= 0,
	DDI_SUSPEND		= 1,
	DDI_PM_SUSPEND		= 2,
	DDI_HOTPLUG_DETACH	= 3
} ddi_detach_cmd_t;

typedef enum {
	DDI_RESET_FORCE = 0
} ddi_reset_cmd_t;

typedef enum {
	PROP_LEN		= 0,
	PROP_LEN_AND_VAL_BUF	= 1,
	PROP_LEN_AND_VAL_ALLOC	= 2,
	PROP_EXISTS		= 3
} ddi_prop_op_t;

typedef void *devmap_cookie_t;
typedef struct as {
       uchar_t  a_flags;
} as_t;

typedef struct pollhead {
	struct polldat *ph_list;
} pollhead_t;

typedef struct dev_info {
	kmutex_t di_lock;
	char di_name[DDI_MAX_NAME_LEN];
	struct dev_ops *di_ops;
	struct cdev *di_cdev;
	spl_class *di_class;
	spl_device *di_device;
	major_t	di_major;
	minor_t di_minor;
	dev_t di_dev;
	unsigned di_minors;
	int di_flags;
	struct list_head di_list;
} dev_info_t;

typedef struct cb_ops {
	int (*cb_open)(dev_t *devp, int flag, int otyp, cred_t *credp);
	int (*cb_close)(dev_t dev, int flag, int otyp, cred_t *credp);
	int (*cb_strategy)(void *bp);
	int (*cb_print)(dev_t dev, char *str);
	int (*cb_dump)(dev_t dev, caddr_t addr, daddr_t blkno, int nblk);
	int (*cb_read)(dev_t dev, struct uio *uiop, cred_t *credp);
	int (*cb_write)(dev_t dev, struct uio *uiop, cred_t *credp);
	int (*cb_ioctl)(dev_t dev, int cmd, intptr_t arg, int mode,
	                cred_t *credp, int *rvalp);
	int (*cb_devmap)(dev_t dev, devmap_cookie_t dhp, offset_t off,
	                 size_t len, size_t *maplen, uint_t model);
	int (*cb_mmap)(dev_t dev, off_t off, int prot);
	int (*cb_segmap)(dev_t dev, off_t off, struct as *asp,
	                 caddr_t *addrp, off_t len, unsigned int prot,
	                 unsigned int maxprot, unsigned int flags,
	                 cred_t *credp);
	int (*cb_chpoll)(dev_t dev, short events, int anyyet,
	                 short *reventsp, struct pollhead **phpp);
	int (*cb_prop_op)(dev_t dev, dev_info_t *dip,
	                  ddi_prop_op_t prop_op, int mod_flags,
	                  char *name, caddr_t valuep, int *length);
	struct streamtab *cb_str;
	int cb_flag;
	int cb_rev;
	int (*cb_aread)(dev_t dev, struct aio_req *aio, cred_t *credp);
	int (*cb_awrite)(dev_t dev, struct aio_req *aio, cred_t *credp);
} cb_ops_t;

typedef struct dev_ops {
	int devo_rev;
	int devo_refcnt;

	int (*devo_getinfo)(dev_info_t *dip,
	                    ddi_info_cmd_t infocmd, void *arg, void **result);
	int (*devo_identify)(dev_info_t *dip);
	int (*devo_probe)(dev_info_t *dip);
	int (*devo_attach)(dev_info_t *dip, ddi_attach_cmd_t cmd);
	int (*devo_detach)(dev_info_t *dip, ddi_detach_cmd_t cmd);
	int (*devo_reset)(dev_info_t *dip, ddi_reset_cmd_t cmd);

	struct cb_ops *devo_cb_ops;
	struct bus_ops *devo_bus_ops;
	int (*devo_power)(dev_info_t *dip, int component, int level);
	int (*devo_quiesce)(dev_info_t *dip);
} dev_ops_t;

typedef struct mod_ops {
	int (*modm_install)(void);
	int (*modm_remove)(void);
	int (*modm_info)(void);
} mod_ops_t;

typedef struct modldrv {
	struct mod_ops *drv_modops;
	char *drv_linkinfo;
	struct dev_ops *drv_dev_ops;
	struct dev_info *drv_dev_info;
} modldrv_t;

#define	MODREV_1			1

#define	D_NEW				0x000
#define D_MP				0x020
#define	D_64BIT				0x200

#define	DEVO_REV			3
#define	CB_REV				1

#define	DDI_SUCCESS			0
#define	DDI_FAILURE			-1

#define	DDI_PSEUDO			"ddi_pseudo"

#define nodev				NULL
#define nochpoll			NULL
#define nulldev				NULL
#define mod_driverops			NULL
#define ddi_prop_op			NULL

#define getminor(x)			(x)
#define getmajor(x)			(x)
#define ddi_driver_major(di)		getmajor(di->di_dev)

#define	DDI_DEV_T_NONE			((dev_t)-1)
#define	DDI_DEV_T_ANY			((dev_t)-2)
#define	DDI_MAJOR_T_UNKNOWN		((major_t)0)

#define	DDI_PROP_DONTPASS		0x0001
#define	DDI_PROP_CANSLEEP		0x0002

#define	GLOBAL_DEV			0x02
#define	NODEBOUND_DEV			0x04
#define	NODESPECIFIC_DEV		0x06
#define	ENUMERATED_DEV			0x08

#define ddi_prop_lookup_string(x1,x2,x3,x4,x5)	(*x5 = NULL)
#define ddi_prop_free(x)			(void)0
#define ddi_root_node()				(void)0

#define mod_install(x)			0
#define mod_remove(x)			0

extern int __ddi_create_minor_node(dev_info_t *dip, char *name, int spec_type,
                                   minor_t minor_num, char *node_type,
				   int flags, struct module *mod);
extern void __ddi_remove_minor_node(dev_info_t *dip, char *name);
extern int ddi_quiesce_not_needed(dev_info_t *dip);
extern int __mod_install(struct modlinkage *modlp);
extern int __mod_remove(struct modlinkage *modlp);
extern int __mod_mknod(char *name, char *type, int major, int minor);

extern int ddi_strtoul(const char *, char **, int, unsigned long *);
extern int ddi_strtol(const char *, char **, int, long *);
extern int ddi_strtoull(const char *, char **, int, unsigned long long *);
extern int ddi_strtoll(const char *, char **, int, long long *);

extern int ddi_copyin(const void *from, void *to, size_t len, int flags);
extern int ddi_copyout(const void *from, void *to, size_t len, int flags);

static __inline__ void ddi_report_dev(dev_info_t *d) { }
static __inline__ void ddi_prop_remove_all(dev_info_t *dip) { }

static __inline__ void
ddi_remove_minor_node(dev_info_t *di, char *name)
{
#ifdef HAVE_GPL_ONLY_SYMBOLS
	/* Cleanup udev (GPL-only symbols required).  This is performed as
	 * part of an inline function to ensure that these symbols are not
	 * linked against the SPL which is GPL'ed.  But instead they are
	 * linked against the package building against the SPL to ensure
	 * its license allows linking with GPL-only symbols. */
	if (di->di_class) {
		spl_device_destroy(di->di_class, di->di_device, di->di_dev);
		spl_class_destroy(di->di_class);
		di->di_class = NULL;
		di->di_dev = 0;
	}
#else
	/* When we do not have access to the GPL-only device interfaces we
	 * are forced to do something crude.  We unlink the special device
	 * file in /dev/ ourselves from within the kernel.  On the upside we
	 * are already providing this functionality for Solaris, and it is
	 * easy to leverage the Solaris API to perform the unlink. */
	if (strlen(di->di_name) > 0)
		vn_remove(di->di_name, UIO_SYSSPACE, RMFILE);

#endif /* HAVE_GPL_ONLY_SYMBOLS */

	__ddi_remove_minor_node(di, name);
}

static __inline__ int
ddi_create_minor_node(dev_info_t *di, char *name, int spec_type,
                      minor_t minor_num, char *node_type, int flags)
{
	int rc;

	rc = __ddi_create_minor_node(di, name, spec_type, minor_num,
	                             node_type, flags, THIS_MODULE);
	if (rc)
		return rc;

#ifdef HAVE_GPL_ONLY_SYMBOLS
	/* Setup udev (GPL-only symbols required).  This is performed as
	 * part of an inline function to ensure that these symbols are not
	 * linked against the SPL which is GPL'ed.  But instead they are
	 * linked against the package building against the SPL to ensure
	 * its license allows linking with GPL-only symbols. */
	di->di_class = spl_class_create(THIS_MODULE, name);
	if (IS_ERR(di->di_class)) {
		rc = PTR_ERR(di->di_class);
		di->di_class = NULL;
		ddi_remove_minor_node(di, name);
		return DDI_FAILURE;
	}

	/* Do not append a 0 to devices with minor nums of 0 */
	di->di_device = spl_device_create(di->di_class, NULL, di->di_dev, NULL,
					  (di->di_minor == 0) ? "%s" : "%s%d",
					  name, di->di_minor);
#else
	/* When we do not have access to the GPL-only device interfaces we
	 * are forced to do something horible.  We use a user mode helper to
	 * create the special device file in /dev/.  By futher extending the
	 * Solaris vnode implementation we could potentially do a vn_create()
	 * from within the kernel but that's still a hack. */
	if (name) {
		rc = __mod_mknod(di->di_name, "c", di->di_major, di->di_minor);
		if (rc) {
			ddi_remove_minor_node(di, name);
		}
	}

#endif /* HAVE_GPL_ONLY_SYMBOLS */

	return rc;
}

#undef mod_install
#undef mod_remove

#define mod_install			__mod_install
#define mod_remove			__mod_remove

#endif /* SPL_SUNDDI_H */
