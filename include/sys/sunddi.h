#ifndef _SPL_SUNDDI_H
#define _SPL_SUNDDI_H

#include <sys/cred.h>
#include <sys/uio.h>
#include <sys/sunldi.h>
#include <sys/mutex.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/list.h>
#include <linux/device.h>

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
	struct dev_ops *di_ops;
	struct cdev *di_cdev;
	struct class *di_class;
	major_t	di_major;
	minor_t di_minor;
	dev_t di_dev;
	unsigned di_minors;
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

#define mod_install(x)			0
#define mod_remove(x)			0

extern int __ddi_create_minor_node(dev_info_t *dip, char *name, int spec_type,
                                   minor_t minor_num, char *node_type,
				   int flag, struct module *mod);
extern void __ddi_remove_minor_node(dev_info_t *dip, char *name);
extern int __mod_install(struct modlinkage *modlp);
extern int __mod_remove(struct modlinkage *modlp);

static __inline__ void ddi_report_dev(dev_info_t *d) { }
static __inline__ void ddi_prop_remove_all(dev_info_t *dip) { }

static __inline__ int
ddi_create_minor_node(dev_info_t *di, char *name, int spec_type,
                      minor_t minor_num, char *node_type, int flag)
{
	return __ddi_create_minor_node(di, name, spec_type, minor_num,
	                               node_type, flag, THIS_MODULE);

}

#undef mod_install
#undef mod_remove

#define ddi_remove_minor_node		__ddi_remove_minor_node
#define mod_install			__mod_install
#define mod_remove			__mod_remove

#endif /* SPL_SUNDDI_H */
