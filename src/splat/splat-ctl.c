/*
 * My intent is the create a loadable kzt (kernel ZFS test) module
 * which can be used as an access point to run in kernel ZFS regression
 * tests.  Why do we need this when we have ztest?  Well ztest.c only
 * excersises the ZFS code proper, it cannot be used to validate the
 * linux kernel shim primatives.  This also provides a nice hook for
 * any other in kernel regression tests we wish to run such as direct
 * in-kernel tests against the DMU.
 *
 * The basic design is the kzt module is that it is constructed of
 * various kzt_* source files each of which contains regression tests.
 * For example the kzt_linux_kmem.c file contains tests for validating
 * kmem correctness.  When the kzt module is loaded kzt_*_init()
 * will be called for each subsystems tests, similarly kzt_*_fini() is
 * called when the kzt module is removed.  Each test can then be
 * run by making an ioctl() call from a userspace control application
 * to pick the subsystem and test which should be run.
 *
 * Author: Brian Behlendorf
 */

#include <splat-ctl.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
#include <linux/devfs_fs_kernel.h>
#endif

#include <linux/cdev.h>


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
static struct class_simple *kzt_class;
#else
static struct class *kzt_class;
#endif
static struct list_head kzt_module_list;
static spinlock_t kzt_module_lock;

static int
kzt_open(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	kzt_info_t *info;

	if (minor >= KZT_MINORS)
		return -ENXIO;

	info = (kzt_info_t *)kmalloc(sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		return -ENOMEM;

	spin_lock_init(&info->info_lock);
	info->info_size = KZT_INFO_BUFFER_SIZE;
	info->info_buffer = (char *)vmalloc(KZT_INFO_BUFFER_SIZE);
	if (info->info_buffer == NULL) {
		kfree(info);
		return -ENOMEM;
	}

	info->info_head = info->info_buffer;
	file->private_data = (void *)info;

	kzt_print(file, "Kernel ZFS Tests %s\n", KZT_VERSION);

        return 0;
}

static int
kzt_release(struct inode *inode, struct file *file)
{
	unsigned int minor = iminor(inode);
	kzt_info_t *info = (kzt_info_t *)file->private_data;

	if (minor >= KZT_MINORS)
		return -ENXIO;

	ASSERT(info);
	ASSERT(info->info_buffer);

	vfree(info->info_buffer);
	kfree(info);

	return 0;
}

static int
kzt_buffer_clear(struct file *file, kzt_cfg_t *kcfg, unsigned long arg)
{
	kzt_info_t *info = (kzt_info_t *)file->private_data;

	ASSERT(info);
	ASSERT(info->info_buffer);

	spin_lock(&info->info_lock);
	memset(info->info_buffer, 0, info->info_size);
	info->info_head = info->info_buffer;
	spin_unlock(&info->info_lock);

	return 0;
}

static int
kzt_buffer_size(struct file *file, kzt_cfg_t *kcfg, unsigned long arg)
{
	kzt_info_t *info = (kzt_info_t *)file->private_data;
	char *buf;
	int min, size, rc = 0;

	ASSERT(info);
	ASSERT(info->info_buffer);

	spin_lock(&info->info_lock);
	if (kcfg->cfg_arg1 > 0) {

		size = kcfg->cfg_arg1;
		buf = (char *)vmalloc(size);
		if (buf == NULL) {
			rc = -ENOMEM;
			goto out;
		}

		/* Zero fill and truncate contents when coping buffer */
		min = ((size < info->info_size) ? size : info->info_size);
		memset(buf, 0, size);
		memcpy(buf, info->info_buffer, min);
		vfree(info->info_buffer);
		info->info_size = size;
		info->info_buffer = buf;
		info->info_head = info->info_buffer;
	}

	kcfg->cfg_rc1 = info->info_size;

	if (copy_to_user((struct kzt_cfg_t __user *)arg, kcfg, sizeof(*kcfg)))
		rc = -EFAULT;
out:
	spin_unlock(&info->info_lock);

	return rc;
}


static kzt_subsystem_t *
kzt_subsystem_find(int id) {
	kzt_subsystem_t *sub;

        spin_lock(&kzt_module_lock);
        list_for_each_entry(sub, &kzt_module_list, subsystem_list) {
		if (id == sub->desc.id) {
		        spin_unlock(&kzt_module_lock);
			return sub;
		}
        }
        spin_unlock(&kzt_module_lock);

	return NULL;
}

static int
kzt_subsystem_count(kzt_cfg_t *kcfg, unsigned long arg)
{
	kzt_subsystem_t *sub;
	int i = 0;

        spin_lock(&kzt_module_lock);
        list_for_each_entry(sub, &kzt_module_list, subsystem_list)
		i++;

        spin_unlock(&kzt_module_lock);
	kcfg->cfg_rc1 = i;

	if (copy_to_user((struct kzt_cfg_t __user *)arg, kcfg, sizeof(*kcfg)))
		return -EFAULT;

	return 0;
}

static int
kzt_subsystem_list(kzt_cfg_t *kcfg, unsigned long arg)
{
	kzt_subsystem_t *sub;
	kzt_cfg_t *tmp;
	int size, i = 0;

	/* Structure will be sized large enough for N subsystem entries
	 * which is passed in by the caller.  On exit the number of
	 * entries filled in with valid subsystems will be stored in
	 * cfg_rc1.  If the caller does not provide enough entries
	 * for all subsystems we will truncate the list to avoid overrun.
	 */
	size = sizeof(*tmp) + kcfg->cfg_data.kzt_subsystems.size *
	       sizeof(kzt_user_t);
	tmp = kmalloc(size, GFP_KERNEL);
	if (tmp == NULL)
		return -ENOMEM;

	/* Local 'tmp' is used as the structure copied back to user space */
	memset(tmp, 0, size);
	memcpy(tmp, kcfg, sizeof(*kcfg));

        spin_lock(&kzt_module_lock);
        list_for_each_entry(sub, &kzt_module_list, subsystem_list) {
		strncpy(tmp->cfg_data.kzt_subsystems.descs[i].name,
		        sub->desc.name, KZT_NAME_SIZE);
		strncpy(tmp->cfg_data.kzt_subsystems.descs[i].desc,
		        sub->desc.desc, KZT_DESC_SIZE);
		tmp->cfg_data.kzt_subsystems.descs[i].id = sub->desc.id;

		/* Truncate list if we are about to overrun alloc'ed memory */
		if ((i++) == kcfg->cfg_data.kzt_subsystems.size)
			break;
        }
        spin_unlock(&kzt_module_lock);
	tmp->cfg_rc1 = i;

	if (copy_to_user((struct kzt_cfg_t __user *)arg, tmp, size)) {
		kfree(tmp);
		return -EFAULT;
	}

	kfree(tmp);
	return 0;
}

static int
kzt_test_count(kzt_cfg_t *kcfg, unsigned long arg)
{
	kzt_subsystem_t *sub;
	kzt_test_t *test;
	int i = 0;

	/* Subsystem ID passed as arg1 */
	sub = kzt_subsystem_find(kcfg->cfg_arg1);
	if (sub == NULL)
		return -EINVAL;

        spin_lock(&(sub->test_lock));
        list_for_each_entry(test, &(sub->test_list), test_list)
		i++;

        spin_unlock(&(sub->test_lock));
	kcfg->cfg_rc1 = i;

	if (copy_to_user((struct kzt_cfg_t __user *)arg, kcfg, sizeof(*kcfg)))
		return -EFAULT;

	return 0;
}

static int
kzt_test_list(kzt_cfg_t *kcfg, unsigned long arg)
{
	kzt_subsystem_t *sub;
	kzt_test_t *test;
	kzt_cfg_t *tmp;
	int size, i = 0;

	/* Subsystem ID passed as arg1 */
	sub = kzt_subsystem_find(kcfg->cfg_arg1);
	if (sub == NULL)
		return -EINVAL;

	/* Structure will be sized large enough for N test entries
	 * which is passed in by the caller.  On exit the number of
	 * entries filled in with valid tests will be stored in
	 * cfg_rc1.  If the caller does not provide enough entries
	 * for all tests we will truncate the list to avoid overrun.
	 */
	size = sizeof(*tmp)+kcfg->cfg_data.kzt_tests.size*sizeof(kzt_user_t);
	tmp = kmalloc(size, GFP_KERNEL);
	if (tmp == NULL)
		return -ENOMEM;

	/* Local 'tmp' is used as the structure copied back to user space */
	memset(tmp, 0, size);
	memcpy(tmp, kcfg, sizeof(*kcfg));

        spin_lock(&(sub->test_lock));
        list_for_each_entry(test, &(sub->test_list), test_list) {
		strncpy(tmp->cfg_data.kzt_tests.descs[i].name,
		        test->desc.name, KZT_NAME_SIZE);
		strncpy(tmp->cfg_data.kzt_tests.descs[i].desc,
		        test->desc.desc, KZT_DESC_SIZE);
		tmp->cfg_data.kzt_tests.descs[i].id = test->desc.id;

		/* Truncate list if we are about to overrun alloc'ed memory */
		if ((i++) == kcfg->cfg_data.kzt_tests.size)
			break;
        }
        spin_unlock(&(sub->test_lock));
	tmp->cfg_rc1 = i;

	if (copy_to_user((struct kzt_cfg_t __user *)arg, tmp, size)) {
		kfree(tmp);
		return -EFAULT;
	}

	kfree(tmp);
	return 0;
}

static int
kzt_validate(struct file *file, kzt_subsystem_t *sub, int cmd, void *arg)
{
        kzt_test_t *test;

        spin_lock(&(sub->test_lock));
        list_for_each_entry(test, &(sub->test_list), test_list) {
                if (test->desc.id == cmd) {
			spin_unlock(&(sub->test_lock));
                        return test->test(file, arg);
                }
        }
        spin_unlock(&(sub->test_lock));

        return -EINVAL;
}

static int
kzt_ioctl_cfg(struct file *file, unsigned long arg)
{
	kzt_cfg_t kcfg;
	int rc = 0;

	if (copy_from_user(&kcfg, (kzt_cfg_t *)arg, sizeof(kcfg)))
		return -EFAULT;

	if (kcfg.cfg_magic != KZT_CFG_MAGIC) {
		kzt_print(file, "Bad config magic 0x%x != 0x%x\n",
		          kcfg.cfg_magic, KZT_CFG_MAGIC);
		return -EINVAL;
	}

	switch (kcfg.cfg_cmd) {
		case KZT_CFG_BUFFER_CLEAR:
			/* cfg_arg1 - Unused
			 * cfg_rc1  - Unused
			 */
			rc = kzt_buffer_clear(file, &kcfg, arg);
			break;
		case KZT_CFG_BUFFER_SIZE:
			/* cfg_arg1 - 0 - query size; >0 resize
			 * cfg_rc1  - Set to current buffer size
			 */
			rc = kzt_buffer_size(file, &kcfg, arg);
			break;
		case KZT_CFG_SUBSYSTEM_COUNT:
			/* cfg_arg1 - Unused
			 * cfg_rc1  - Set to number of subsystems
			 */
			rc = kzt_subsystem_count(&kcfg, arg);
			break;
		case KZT_CFG_SUBSYSTEM_LIST:
			/* cfg_arg1 - Unused
			 * cfg_rc1  - Set to number of subsystems
			 * cfg_data.kzt_subsystems - Populated with subsystems
			 */
			rc = kzt_subsystem_list(&kcfg, arg);
			break;
		case KZT_CFG_TEST_COUNT:
			/* cfg_arg1 - Set to a target subsystem
			 * cfg_rc1  - Set to number of tests
			 */
			rc = kzt_test_count(&kcfg, arg);
			break;
		case KZT_CFG_TEST_LIST:
			/* cfg_arg1 - Set to a target subsystem
			 * cfg_rc1  - Set to number of tests
			 * cfg_data.kzt_subsystems - Populated with tests
			 */
			rc = kzt_test_list(&kcfg, arg);
			break;
		default:
			kzt_print(file, "Bad config command %d\n", kcfg.cfg_cmd);
			rc = -EINVAL;
			break;
	}

	return rc;
}

static int
kzt_ioctl_cmd(struct file *file, unsigned long arg)
{
	kzt_subsystem_t *sub;
	kzt_cmd_t kcmd;
	int rc = -EINVAL;
	void *data = NULL;

	if (copy_from_user(&kcmd, (kzt_cfg_t *)arg, sizeof(kcmd)))
		return -EFAULT;

	if (kcmd.cmd_magic != KZT_CMD_MAGIC) {
		kzt_print(file, "Bad command magic 0x%x != 0x%x\n",
		          kcmd.cmd_magic, KZT_CFG_MAGIC);
		return -EINVAL;
	}

	/* Allocate memory for any opaque data the caller needed to pass on */
	if (kcmd.cmd_data_size > 0) {
		data = (void *)kmalloc(kcmd.cmd_data_size, GFP_KERNEL);
		if (data == NULL)
			return -ENOMEM;

		if (copy_from_user(data, (void *)(arg + offsetof(kzt_cmd_t,
		                   cmd_data_str)), kcmd.cmd_data_size)) {
			kfree(data);
			return -EFAULT;
		}
	}

	sub = kzt_subsystem_find(kcmd.cmd_subsystem);
	if (sub != NULL)
		rc = kzt_validate(file, sub, kcmd.cmd_test, data);
	else
		rc = -EINVAL;

	if (data != NULL)
		kfree(data);

	return rc;
}

static int
kzt_ioctl(struct inode *inode, struct file *file,
	  unsigned int cmd, unsigned long arg)
{
        unsigned int minor = iminor(file->f_dentry->d_inode);
	int rc = 0;

	/* Ignore tty ioctls */
	if ((cmd & 0xffffff00) == ((int)'T') << 8)
		return -ENOTTY;

	if (minor >= KZT_MINORS)
		return -ENXIO;

	switch (cmd) {
		case KZT_CFG:
			rc = kzt_ioctl_cfg(file, arg);
			break;
		case KZT_CMD:
			rc = kzt_ioctl_cmd(file, arg);
			break;
		default:
			kzt_print(file, "Bad ioctl command %d\n", cmd);
			rc = -EINVAL;
			break;
	}

	return rc;
}

/* I'm not sure why you would want to write in to this buffer from
 * user space since its principle use is to pass test status info
 * back to the user space, but I don't see any reason to prevent it.
 */
static ssize_t kzt_write(struct file *file, const char __user *buf,
                         size_t count, loff_t *ppos)
{
        unsigned int minor = iminor(file->f_dentry->d_inode);
	kzt_info_t *info = (kzt_info_t *)file->private_data;
	int rc = 0;

	if (minor >= KZT_MINORS)
		return -ENXIO;

	ASSERT(info);
	ASSERT(info->info_buffer);

	spin_lock(&info->info_lock);

	/* Write beyond EOF */
	if (*ppos >= info->info_size) {
		rc = -EFBIG;
		goto out;
	}

	/* Resize count if beyond EOF */
	if (*ppos + count > info->info_size)
		count = info->info_size - *ppos;

	if (copy_from_user(info->info_buffer, buf, count)) {
		rc = -EFAULT;
		goto out;
	}

	*ppos += count;
	rc = count;
out:
	spin_unlock(&info->info_lock);
	return rc;
}

static ssize_t kzt_read(struct file *file, char __user *buf,
		        size_t count, loff_t *ppos)
{
        unsigned int minor = iminor(file->f_dentry->d_inode);
	kzt_info_t *info = (kzt_info_t *)file->private_data;
	int rc = 0;

	if (minor >= KZT_MINORS)
		return -ENXIO;

	ASSERT(info);
	ASSERT(info->info_buffer);

	spin_lock(&info->info_lock);

	/* Read beyond EOF */
	if (*ppos >= info->info_size)
		goto out;

	/* Resize count if beyond EOF */
	if (*ppos + count > info->info_size)
		count = info->info_size - *ppos;

	if (copy_to_user(buf, info->info_buffer + *ppos, count)) {
		rc = -EFAULT;
		goto out;
	}

	*ppos += count;
	rc = count;
out:
	spin_unlock(&info->info_lock);
	return rc;
}

static loff_t kzt_seek(struct file *file, loff_t offset, int origin)
{
        unsigned int minor = iminor(file->f_dentry->d_inode);
	kzt_info_t *info = (kzt_info_t *)file->private_data;
	int rc = -EINVAL;

	if (minor >= KZT_MINORS)
		return -ENXIO;

	ASSERT(info);
	ASSERT(info->info_buffer);

	spin_lock(&info->info_lock);

	switch (origin) {
	case 0: /* SEEK_SET - No-op just do it */
		break;
	case 1: /* SEEK_CUR - Seek from current */
		offset = file->f_pos + offset;
		break;
	case 2: /* SEEK_END - Seek from end */
		offset = info->info_size + offset;
		break;
	}

	if (offset >= 0) {
		file->f_pos = offset;
		file->f_version = 0;
		rc = offset;
	}

	spin_unlock(&info->info_lock);

	return rc;
}

static struct file_operations kzt_fops = {
	.owner   = THIS_MODULE,
	.open    = kzt_open,
	.release = kzt_release,
	.ioctl   = kzt_ioctl,
	.read    = kzt_read,
	.write   = kzt_write,
	.llseek  = kzt_seek,
};

static struct cdev kzt_cdev = {
	.owner  =	THIS_MODULE,
	.kobj   =	{ .name = "kztctl", },
};

static int __init
kzt_init(void)
{
	dev_t dev;
	int rc;

	spin_lock_init(&kzt_module_lock);
	INIT_LIST_HEAD(&kzt_module_list);

	KZT_SUBSYSTEM_INIT(kmem);
	KZT_SUBSYSTEM_INIT(taskq);
	KZT_SUBSYSTEM_INIT(krng);
	KZT_SUBSYSTEM_INIT(mutex);
	KZT_SUBSYSTEM_INIT(condvar);
	KZT_SUBSYSTEM_INIT(thread);
	KZT_SUBSYSTEM_INIT(rwlock);
	KZT_SUBSYSTEM_INIT(time);

	dev = MKDEV(KZT_MAJOR, 0);
        if ((rc = register_chrdev_region(dev, KZT_MINORS, "kztctl")))
		goto error;

	/* Support for registering a character driver */
	cdev_init(&kzt_cdev, &kzt_fops);
	if ((rc = cdev_add(&kzt_cdev, dev, KZT_MINORS))) {
		printk(KERN_ERR "kzt: Error adding cdev, %d\n", rc);
		kobject_put(&kzt_cdev.kobj);
		unregister_chrdev_region(dev, KZT_MINORS);
		goto error;
	}

	/* Support for udev make driver info available in sysfs */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
        kzt_class = class_simple_create(THIS_MODULE, "kzt");
#else
        kzt_class = class_create(THIS_MODULE, "kzt");
#endif
	if (IS_ERR(kzt_class)) {
		rc = PTR_ERR(kzt_class);
		printk(KERN_ERR "kzt: Error creating kzt class, %d\n", rc);
		cdev_del(&kzt_cdev);
		unregister_chrdev_region(dev, KZT_MINORS);
		goto error;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
	class_simple_device_add(kzt_class, MKDEV(KZT_MAJOR, 0),
	                        NULL, "kztctl");
#else
	class_device_create(kzt_class, NULL, MKDEV(KZT_MAJOR, 0),
	                    NULL, "kztctl");
#endif

	printk(KERN_INFO "kzt: Kernel ZFS Tests %s Loaded\n", KZT_VERSION);
	return 0;
error:
	printk(KERN_ERR "kzt: Error registering kzt device, %d\n", rc);
	return rc;
}

static void
kzt_fini(void)
{
	dev_t dev = MKDEV(KZT_MAJOR, 0);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,18)
        class_simple_device_remove(dev);
        class_simple_destroy(kzt_class);
        devfs_remove("kzt/kztctl");
        devfs_remove("kzt");
#else
        class_device_destroy(kzt_class, dev);
        class_destroy(kzt_class);
#endif
        cdev_del(&kzt_cdev);
        unregister_chrdev_region(dev, KZT_MINORS);

	KZT_SUBSYSTEM_FINI(time);
	KZT_SUBSYSTEM_FINI(rwlock);
	KZT_SUBSYSTEM_FINI(thread);
	KZT_SUBSYSTEM_FINI(condvar);
	KZT_SUBSYSTEM_FINI(mutex);
	KZT_SUBSYSTEM_FINI(krng);
	KZT_SUBSYSTEM_FINI(taskq);
	KZT_SUBSYSTEM_FINI(kmem);

	ASSERT(list_empty(&kzt_module_list));
	printk(KERN_INFO "kzt: Kernel ZFS Tests %s Unloaded\n", KZT_VERSION);
}

module_init(kzt_init);
module_exit(kzt_fini);

MODULE_AUTHOR("Lawrence Livermore National Labs");
MODULE_DESCRIPTION("Kernel ZFS Test");
MODULE_LICENSE("GPL");

