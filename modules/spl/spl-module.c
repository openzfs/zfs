/*
 *  This file is part of the SPL: Solaris Porting Layer.
 *
 *  Copyright (c) 2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory
 *  Written by:
 *          Brian Behlendorf <behlendorf1@llnl.gov>,
 *          Herb Wartens <wartens2@llnl.gov>,
 *          Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-235197
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include <sys/sysmacros.h>
#include <sys/sunddi.h>

#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_MODULE

static spinlock_t dev_info_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(dev_info_list);

static struct dev_info *
get_dev_info(dev_t dev)
{
	struct dev_info *di;

	spin_lock(&dev_info_lock);

        list_for_each_entry(di, &dev_info_list, di_list)
		if (di->di_dev == dev)
			goto out;

	di = NULL;
out:
	spin_unlock(&dev_info_lock);
	return di;
}

static int
mod_generic_ioctl(struct inode *ino, struct file *filp,
		  unsigned int cmd, unsigned long arg)
{
	struct dev_info *di;
	int rc, flags = 0, rvalp = 0;
	cred_t *cr = NULL;

	di = get_dev_info(MKDEV(imajor(ino), iminor(ino)));
	if (di == NULL)
		return EINVAL;

	rc = di->di_ops->devo_cb_ops->cb_ioctl(di->di_dev,
	                                       (int)cmd,(intptr_t)arg,
	                                       flags, cr, &rvalp);
	return rc;
}

int
__ddi_create_minor_node(dev_info_t *di, char *name, int spec_type,
                        minor_t minor_num, char *node_type,
		        int flags, struct module *mod)
{
	struct cdev *cdev;
	struct dev_ops *dev_ops;
	struct cb_ops *cb_ops;
	struct file_operations *fops;
	int rc;
	ENTRY;

	ASSERT(spec_type == S_IFCHR);
	ASSERT(minor_num < di->di_minors);
	ASSERT(!strcmp(node_type, DDI_PSEUDO));

	fops = kzalloc(sizeof(struct file_operations), GFP_KERNEL);
	if (fops == NULL)
		RETURN(DDI_FAILURE);

	cdev = cdev_alloc();
	if (cdev == NULL) {
		kfree(fops);
		RETURN(DDI_FAILURE);
	}

	cdev->ops = fops;

	mutex_enter(&di->di_lock);
	dev_ops = di->di_ops;
	ASSERT(dev_ops);
	cb_ops = di->di_ops->devo_cb_ops;
	ASSERT(cb_ops);

	/* Setup the fops to cb_ops mapping */
	fops->owner = mod;
	if (cb_ops->cb_ioctl)
		fops->ioctl = mod_generic_ioctl;

#if 0
	if (cb_ops->cb_open)
		fops->open = mod_generic_open;

	if (cb_ops->cb_close)
		fops->release = mod_generic_close;

	if (cb_ops->cb_read)
		fops->read = mod_generic_read;

	if (cb_ops->cb_write)
		fops->write = mod_generic_write;
#endif
	/* XXX: Currently unsupported operations */
	ASSERT(cb_ops->cb_open == NULL);
	ASSERT(cb_ops->cb_close == NULL);
	ASSERT(cb_ops->cb_read == NULL);
	ASSERT(cb_ops->cb_write == NULL);
	ASSERT(cb_ops->cb_strategy == NULL);
	ASSERT(cb_ops->cb_print == NULL);
	ASSERT(cb_ops->cb_dump == NULL);
	ASSERT(cb_ops->cb_devmap == NULL);
	ASSERT(cb_ops->cb_mmap == NULL);
	ASSERT(cb_ops->cb_segmap == NULL);
	ASSERT(cb_ops->cb_chpoll == NULL);
	ASSERT(cb_ops->cb_prop_op == NULL);
	ASSERT(cb_ops->cb_str == NULL);
	ASSERT(cb_ops->cb_aread == NULL);
	ASSERT(cb_ops->cb_awrite == NULL);

	di->di_cdev  = cdev;
	di->di_flags = flags;
	di->di_minor = minor_num;
	di->di_dev   = MKDEV(di->di_major, di->di_minor);

	rc = cdev_add(cdev, di->di_dev, 1);
	if (rc) {
		CERROR("Error adding cdev, %d\n", rc);
		kfree(fops);
		cdev_del(cdev);
		mutex_exit(&di->di_lock);
		RETURN(DDI_FAILURE);
	}

	spin_lock(&dev_info_lock);
	list_add(&di->di_list, &dev_info_list);
	spin_unlock(&dev_info_lock);

	mutex_exit(&di->di_lock);

	RETURN(DDI_SUCCESS);
}
EXPORT_SYMBOL(__ddi_create_minor_node);

static void
__ddi_remove_minor_node_locked(dev_info_t *di, char *name)
{
	if (di->di_cdev) {
		cdev_del(di->di_cdev);
		di->di_cdev = NULL;
	}

	spin_lock(&dev_info_lock);
        list_del_init(&di->di_list);
	spin_unlock(&dev_info_lock);
}

void
__ddi_remove_minor_node(dev_info_t *di, char *name)
{
	ENTRY;
	mutex_enter(&di->di_lock);
	__ddi_remove_minor_node_locked(di, name);
	mutex_exit(&di->di_lock);
	EXIT;
}
EXPORT_SYMBOL(__ddi_remove_minor_node);

int
ddi_quiesce_not_needed(dev_info_t *dip)
{
	RETURN(DDI_SUCCESS);
}

#if 0
static int
mod_generic_open(struct inode *, struct file *)
{
	open(dev_t *devp, int flags, int otyp, cred_t *credp);
}

static int
mod_generic_close(struct inode *, struct file *)
{
	close(dev_t dev, int flags, int otyp, cred_t *credp);
}

static ssize_t
mod_generic_read(struct file *, char __user *, size_t, loff_t *)
{
	read(dev_t dev, struct uio *uiop, cred_t *credp);
}

static ssize_t
mod_generic_write(struct file *, const char __user *, size_t, loff_t *)
{
	write(dev_t dev, struct uio *uiop, cred_t *credp);
}
#endif

static struct dev_info *
dev_info_alloc(major_t major, minor_t minors, struct dev_ops *ops) {
	struct dev_info *di;

	di = kmalloc(sizeof(struct dev_info), GFP_KERNEL);
	if (di == NULL)
		return NULL;

	mutex_init(&di->di_lock, NULL, MUTEX_DEFAULT, NULL);
	INIT_LIST_HEAD(&di->di_list);
	di->di_ops = ops;
	di->di_class = NULL;
	di->di_cdev = NULL;
	di->di_major = major;
	di->di_minor = 0;
	di->di_minors = minors;
	di->di_dev = 0;

	return di;
}

static void
dev_info_free(struct dev_info *di)
{
	mutex_enter(&di->di_lock);
	__ddi_remove_minor_node_locked(di, NULL);
	mutex_exit(&di->di_lock);
	mutex_destroy(&di->di_lock);
	kfree(di);
}

int
__mod_install(struct modlinkage *modlp)
{
	struct modldrv *drv = modlp->ml_modldrv;
	struct dev_info *di;
	int rc;
	ENTRY;

	di = dev_info_alloc(modlp->ml_major, modlp->ml_minors,
			    drv->drv_dev_ops);
	if (di == NULL)
		RETURN(ENOMEM);

	/* XXX: Really we need to be calling devo_probe if it's available
	 * and then calling devo_attach for each device discovered.  However
	 * for now we just call it once and let the app sort it out.
	 */
	rc = drv->drv_dev_ops->devo_attach(di, DDI_ATTACH);
	if (rc != DDI_SUCCESS) {
		dev_info_free(di);
		RETURN(rc);
	}

	drv->drv_dev_info = di;

	RETURN(DDI_SUCCESS);
}
EXPORT_SYMBOL(__mod_install);

int
__mod_remove(struct modlinkage *modlp)
{
	struct modldrv *drv = modlp->ml_modldrv;
	struct dev_info *di = drv->drv_dev_info;
	int rc;
	ENTRY;

	rc = drv->drv_dev_ops->devo_detach(di, DDI_DETACH);
	if (rc != DDI_SUCCESS)
		RETURN(rc);

	dev_info_free(di);
	drv->drv_dev_info = NULL;

	RETURN(DDI_SUCCESS);
}
EXPORT_SYMBOL(__mod_remove);

int
ldi_ident_from_mod(struct modlinkage *modlp, ldi_ident_t *lip)
{
	ldi_ident_t li;
	ENTRY;

	ASSERT(modlp);
	ASSERT(lip);

	li = kmalloc(sizeof(struct ldi_ident), GFP_KERNEL);
	if (li == NULL)
		RETURN(ENOMEM);

	li->li_dev = MKDEV(modlp->ml_major, 0);
	*lip = li;

	RETURN(0);
}
EXPORT_SYMBOL(ldi_ident_from_mod);

void
ldi_ident_release(ldi_ident_t lip)
{
	ENTRY;
	ASSERT(lip);
	kfree(lip);
	EXIT;
}
EXPORT_SYMBOL(ldi_ident_release);
