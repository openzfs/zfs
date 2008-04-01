#include <sys/sysmacros.h>
#include <sys/sunddi.h>
#include "config.h"

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
	int rc, flag = 0, rvalp = 0;
	cred_t *cr = NULL;

	di = get_dev_info(MKDEV(imajor(ino), iminor(ino)));
	if (di == NULL)
		return EINVAL;

	rc = di->di_ops->devo_cb_ops->cb_ioctl(di->di_dev,
	                                       (int)cmd,(intptr_t)arg,
	                                       flag, cr, &rvalp);
	return rc;
}

int
__ddi_create_minor_node(dev_info_t *di, char *name, int spec_type,
                        minor_t minor_num, char *node_type,
		        int flag, struct module *mod)
{
	struct cdev *cdev;
	struct dev_ops *dev_ops;
	struct cb_ops *cb_ops;
	struct file_operations *fops;
	int rc;

	BUG_ON(spec_type != S_IFCHR);
	BUG_ON(minor_num >= di->di_minors);
	BUG_ON(strcmp(node_type, DDI_PSEUDO));
	BUG_ON(flag != 0);

	fops = kzalloc(sizeof(struct file_operations), GFP_KERNEL);
	if (fops == NULL)
		return DDI_FAILURE;

	cdev = cdev_alloc();
	if (cdev == NULL) {
		kfree(fops);
		return DDI_FAILURE;
	}

	cdev->ops = fops;

	mutex_enter(&di->di_lock);
	dev_ops = di->di_ops;
	BUG_ON(dev_ops == NULL);
	cb_ops = di->di_ops->devo_cb_ops;
	BUG_ON(cb_ops == NULL);

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
	BUG_ON(cb_ops->cb_open != NULL);
	BUG_ON(cb_ops->cb_close != NULL);
	BUG_ON(cb_ops->cb_read != NULL);
	BUG_ON(cb_ops->cb_write != NULL);
	BUG_ON(cb_ops->cb_strategy != NULL);
	BUG_ON(cb_ops->cb_print != NULL);
	BUG_ON(cb_ops->cb_dump != NULL);
	BUG_ON(cb_ops->cb_devmap != NULL);
	BUG_ON(cb_ops->cb_mmap != NULL);
	BUG_ON(cb_ops->cb_segmap != NULL);
	BUG_ON(cb_ops->cb_chpoll != NULL);
	BUG_ON(cb_ops->cb_prop_op != NULL);
	BUG_ON(cb_ops->cb_str != NULL);
	BUG_ON(cb_ops->cb_aread != NULL);
	BUG_ON(cb_ops->cb_awrite != NULL);

	di->di_minor = minor_num;
	di->di_dev = MKDEV(di->di_major, di->di_minor);

	rc = cdev_add(cdev, di->di_dev, 1);
	if (rc) {
		printk("spl: Error adding cdev, %d\n", rc);
		kfree(fops);
		cdev_del(cdev);
		mutex_exit(&di->di_lock);
		return DDI_FAILURE;
	}

	di->di_class = class_create(THIS_MODULE, name);
	if (IS_ERR(di->di_class)) {
                rc = PTR_ERR(di->di_class);
                printk("spl: Error creating %s class, %d\n", name, rc);
		kfree(fops);
                cdev_del(di->di_cdev);
		mutex_exit(&di->di_lock);
		return DDI_FAILURE;
	}

	/* Do not append a 0 to devices with minor nums of 0 */
	if (di->di_minor == 0) {
	        class_device_create(di->di_class, NULL, di->di_dev,
		                    NULL, "%s", name);
	} else {
	        class_device_create(di->di_class, NULL, di->di_dev,
		                    NULL, "%s%d", name, di->di_minor);
	}

	di->di_cdev = cdev;

	spin_lock(&dev_info_lock);
	list_add(&di->di_list, &dev_info_list);
	spin_unlock(&dev_info_lock);

	mutex_exit(&di->di_lock);

	return DDI_SUCCESS;
}
EXPORT_SYMBOL(__ddi_create_minor_node);

static void
__ddi_remove_minor_node_locked(dev_info_t *di, char *name)
{
	if (di->di_class) {
		class_device_destroy(di->di_class, di->di_dev);
		class_destroy(di->di_class);

		di->di_class = NULL;
		di->di_dev = 0;
	}

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
	mutex_enter(&di->di_lock);
	__ddi_remove_minor_node_locked(di, name);
	mutex_exit(&di->di_lock);
}
EXPORT_SYMBOL(ddi_remove_minor_node);

#if 0
static int
mod_generic_open(struct inode *, struct file *)
{
	open(dev_t *devp, int flag, int otyp, cred_t *credp);
}

static int
mod_generic_close(struct inode *, struct file *)
{
	close(dev_t dev, int flag, int otyp, cred_t *credp);
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

	di = dev_info_alloc(modlp->ml_major, modlp->ml_minors,
			    drv->drv_dev_ops);
	if (di == NULL)
		return ENOMEM;

	/* XXX: Really we need to be calling devo_probe if it's available
	 * and then calling devo_attach for each device discovered.  However
	 * for now we just call it once and let the app sort it out.
	 */
	rc = drv->drv_dev_ops->devo_attach(di, DDI_ATTACH);
	if (rc != DDI_SUCCESS) {
		dev_info_free(di);
		return rc;
	}

	drv->drv_dev_info = di;

	return DDI_SUCCESS;
}
EXPORT_SYMBOL(__mod_install);

int
__mod_remove(struct modlinkage *modlp)
{
	struct modldrv *drv = modlp->ml_modldrv;
	struct dev_info *di = drv->drv_dev_info;
	int rc;

	rc = drv->drv_dev_ops->devo_detach(di, DDI_DETACH);
	if (rc != DDI_SUCCESS)
		return rc;

	dev_info_free(di);
	drv->drv_dev_info = NULL;

	return DDI_SUCCESS;
}
EXPORT_SYMBOL(__mod_remove);

int
ldi_ident_from_mod(struct modlinkage *modlp, ldi_ident_t *lip)
{
	ldi_ident_t li;

	BUG_ON(modlp == NULL || lip == NULL);

	li = kmalloc(sizeof(struct ldi_ident), GFP_KERNEL);
	if (li == NULL)
		return ENOMEM;

	li->li_dev = MKDEV(modlp->ml_major, 0);
	*lip = li;

	return 0;
}
EXPORT_SYMBOL(ldi_ident_from_mod);

void
ldi_ident_release(ldi_ident_t lip)
{
	BUG_ON(lip == NULL);
	kfree(lip);
}
EXPORT_SYMBOL(ldi_ident_release);
