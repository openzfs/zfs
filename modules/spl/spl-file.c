#include <sys/sysmacros.h>
#include <sys/file.h>
#include "config.h"

/* File interface */

static spinlock_t file_lock = SPIN_LOCK_UNLOCKED;
static LIST_HEAD(file_list);
static kmem_cache_t *file_cache;

/* Function must be called while holding the file_lock */
static file_t *
file_find(int fd)
{
        file_t *fp;

	BUG_ON(!spin_is_locked(&file_lock));

        list_for_each_entry(fp, &file_list,  f_list) {
		if (fd == fp->f_fd) {
			BUG_ON(atomic_read(&fp->f_ref) == 0);
                        return fp;
		}
	}

        return NULL;
} /* file_find() */

file_t *
getf(int fd)
{
	file_t *fp;

	/* Already open just take an extra reference */
	spin_lock(&file_lock);

	fp = file_find(fd);
	if (fp) {
		atomic_inc(&fp->f_ref);
		spin_unlock(&file_lock);
		return fp;
	}

	spin_unlock(&file_lock);

	/* File was not yet opened via the SPL layer create needed bits */
	fp = kmem_cache_alloc(file_cache, 0);
	if (fp == NULL)
		goto out;

	mutex_enter(&fp->f_lock);

	fp->f_vnode = vn_alloc(KM_SLEEP);
	if (fp->f_vnode == NULL)
		goto out_mutex;

	/* XXX: Setup needed vnode stop, open file etc */

	fp->f_file = fget(fd);
	if (fp->f_file == NULL)
		goto out_vnode;

	fp->f_fd = fd;
	atomic_inc(&fp->f_ref);

	spin_lock(&file_lock);
	list_add(&fp->f_list, &file_list);
	spin_unlock(&file_lock);

	mutex_exit(&fp->f_lock);
	return fp;

out_vnode:
	vn_free(fp->f_vnode);
out_mutex:
	mutex_exit(&fp->f_lock);
	kmem_cache_free(file_cache, fp);
out:
        return NULL;
} /* getf() */
EXPORT_SYMBOL(getf);

static void releasef_locked(file_t *fp)
{
	BUG_ON(fp->f_file == NULL);
	BUG_ON(fp->f_vnode == NULL);

	/* Unlinked from list, no refs, safe to free outside mutex */
	fput(fp->f_file);
	vn_free(fp->f_vnode);

	kmem_cache_free(file_cache, fp);
}

void
releasef(int fd)
{
	file_t *fp;

	spin_lock(&file_lock);

	fp = file_find(fd);
	if (fp) {
		atomic_dec(&fp->f_ref);

		if (atomic_read(&fp->f_ref) > 0) {
			spin_unlock(&file_lock);
			return;
		}

	        list_del(&fp->f_list);
		spin_unlock(&file_lock);
		releasef_locked(fp);
	}

	return;
} /* releasef() */
EXPORT_SYMBOL(releasef);

static int
file_cache_constructor(void *buf, void *cdrarg, int kmflags)
{
	file_t *fp = buf;

	atomic_set(&fp->f_ref, 0);
        mutex_init(&fp->f_lock, NULL, MUTEX_DEFAULT, NULL);

        return (0);
} /* file_cache_constructor() */

static void
file_cache_destructor(void *buf, void *cdrarg)
{
	file_t *fp = buf;

	mutex_destroy(&fp->f_lock);
} /* file_cache_destructor() */

int
file_init(void)
{
	file_cache = kmem_cache_create("spl_file_cache", sizeof(file_t), 64,
				       file_cache_constructor,
				       file_cache_destructor,
				       NULL, NULL, NULL, 0);
	return 0;
} /* file_init() */

void file_fini(void)
{
        file_t *fp, *next_fp;
	int leaked = 0;

	spin_lock(&file_lock);

        list_for_each_entry_safe(fp, next_fp, &file_list,  f_list) {
	        list_del(&fp->f_list);
		releasef_locked(fp);
		leaked++;
	}

	kmem_cache_destroy(file_cache);
	file_cache = NULL;
	spin_unlock(&file_lock);

	if (leaked > 0)
		printk("Warning: %d files leaked\n", leaked);

} /* file_fini() */
