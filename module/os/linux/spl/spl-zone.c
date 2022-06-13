/*
 * Copyright (c) 2021 Klara Systems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/mutex.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <linux/file.h>
#include <linux/magic.h>
#include <sys/zone.h>

#if defined(CONFIG_USER_NS)
#include <linux/statfs.h>
#include <linux/proc_ns.h>
#endif

static kmutex_t zone_datasets_lock;
static struct list_head zone_datasets;

typedef struct zone_datasets {
	struct list_head zds_list;	/* zone_datasets linkage */
	struct user_namespace *zds_userns; /* namespace reference */
	struct list_head zds_datasets;	/* datasets for the namespace */
} zone_datasets_t;

typedef struct zone_dataset {
	struct list_head zd_list;	/* zone_dataset linkage */
	size_t zd_dsnamelen;		/* length of name */
	char zd_dsname[0];		/* name of the member dataset */
} zone_dataset_t;

#if defined(CONFIG_USER_NS) && defined(HAVE_USER_NS_COMMON_INUM)
/*
 * Returns:
 * - 0 on success
 * - EBADF if it cannot open the provided file descriptor
 * - ENOTTY if the file itself is a not a user namespace file. We want to
 *   intercept this error in the ZFS layer. We cannot just return one of the
 *   ZFS_ERR_* errors here as we want to preserve the seperation of the ZFS
 *   and the SPL layers.
 */
static int
user_ns_get(int fd, struct user_namespace **userns)
{
	struct kstatfs st;
	struct file *nsfile;
	struct ns_common *ns;
	int error;

	if ((nsfile = fget(fd)) == NULL)
		return (EBADF);
	if (vfs_statfs(&nsfile->f_path, &st) != 0) {
		error = ENOTTY;
		goto done;
	}
	if (st.f_type != NSFS_MAGIC) {
		error = ENOTTY;
		goto done;
	}
	ns = get_proc_ns(file_inode(nsfile));
	if (ns->ops->type != CLONE_NEWUSER) {
		error = ENOTTY;
		goto done;
	}
	*userns = container_of(ns, struct user_namespace, ns);

	error = 0;
done:
	fput(nsfile);

	return (error);
}
#endif /* defined(CONFIG_USER_NS) && defined(HAVE_USER_NS_COMMON_INUM) */

static unsigned int
user_ns_zoneid(struct user_namespace *user_ns)
{
	unsigned int r;

#if defined(HAVE_USER_NS_COMMON_INUM)
	r = user_ns->ns.inum;
#else
	r = user_ns->proc_inum;
#endif

	return (r);
}

static struct zone_datasets *
zone_datasets_lookup(unsigned int nsinum)
{
	zone_datasets_t *zds;

	list_for_each_entry(zds, &zone_datasets, zds_list) {
		if (user_ns_zoneid(zds->zds_userns) == nsinum)
			return (zds);
	}
	return (NULL);
}

#if defined(CONFIG_USER_NS) && defined(HAVE_USER_NS_COMMON_INUM)
static struct zone_dataset *
zone_dataset_lookup(zone_datasets_t *zds, const char *dataset, size_t dsnamelen)
{
	zone_dataset_t *zd;

	list_for_each_entry(zd, &zds->zds_datasets, zd_list) {
		if (zd->zd_dsnamelen != dsnamelen)
			continue;
		if (strncmp(zd->zd_dsname, dataset, dsnamelen) == 0)
			return (zd);
	}

	return (NULL);
}

static int
zone_dataset_cred_check(cred_t *cred)
{

	if (!uid_eq(cred->uid, GLOBAL_ROOT_UID))
		return (EPERM);

	return (0);
}
#endif /* defined(CONFIG_USER_NS) && defined(HAVE_USER_NS_COMMON_INUM) */

static int
zone_dataset_name_check(const char *dataset, size_t *dsnamelen)
{

	if (dataset[0] == '\0' || dataset[0] == '/')
		return (ENOENT);

	*dsnamelen = strlen(dataset);
	/* Ignore trailing slash, if supplied. */
	if (dataset[*dsnamelen - 1] == '/')
		(*dsnamelen)--;

	return (0);
}

int
zone_dataset_attach(cred_t *cred, const char *dataset, int userns_fd)
{
#if defined(CONFIG_USER_NS) && defined(HAVE_USER_NS_COMMON_INUM)
	struct user_namespace *userns;
	zone_datasets_t *zds;
	zone_dataset_t *zd;
	int error;
	size_t dsnamelen;

	if ((error = zone_dataset_cred_check(cred)) != 0)
		return (error);
	if ((error = zone_dataset_name_check(dataset, &dsnamelen)) != 0)
		return (error);
	if ((error = user_ns_get(userns_fd, &userns)) != 0)
		return (error);

	mutex_enter(&zone_datasets_lock);
	zds = zone_datasets_lookup(user_ns_zoneid(userns));
	if (zds == NULL) {
		zds = kmem_alloc(sizeof (zone_datasets_t), KM_SLEEP);
		INIT_LIST_HEAD(&zds->zds_list);
		INIT_LIST_HEAD(&zds->zds_datasets);
		zds->zds_userns = userns;
		/*
		 * Lock the namespace by incresing its refcount to prevent
		 * the namespace ID from being reused.
		 */
		get_user_ns(userns);
		list_add_tail(&zds->zds_list, &zone_datasets);
	} else {
		zd = zone_dataset_lookup(zds, dataset, dsnamelen);
		if (zd != NULL) {
			mutex_exit(&zone_datasets_lock);
			return (EEXIST);
		}
	}

	zd = kmem_alloc(sizeof (zone_dataset_t) + dsnamelen + 1, KM_SLEEP);
	zd->zd_dsnamelen = dsnamelen;
	strncpy(zd->zd_dsname, dataset, dsnamelen);
	zd->zd_dsname[dsnamelen] = '\0';
	INIT_LIST_HEAD(&zd->zd_list);
	list_add_tail(&zd->zd_list, &zds->zds_datasets);

	mutex_exit(&zone_datasets_lock);
	return (0);
#else
	return (ENXIO);
#endif /* defined(CONFIG_USER_NS) && defined(HAVE_USER_NS_COMMON_INUM) */
}
EXPORT_SYMBOL(zone_dataset_attach);

int
zone_dataset_detach(cred_t *cred, const char *dataset, int userns_fd)
{
#if defined(CONFIG_USER_NS) && defined(HAVE_USER_NS_COMMON_INUM)
	struct user_namespace *userns;
	zone_datasets_t *zds;
	zone_dataset_t *zd;
	int error;
	size_t dsnamelen;

	if ((error = zone_dataset_cred_check(cred)) != 0)
		return (error);
	if ((error = zone_dataset_name_check(dataset, &dsnamelen)) != 0)
		return (error);
	if ((error = user_ns_get(userns_fd, &userns)) != 0)
		return (error);

	mutex_enter(&zone_datasets_lock);
	zds = zone_datasets_lookup(user_ns_zoneid(userns));
	if (zds != NULL)
		zd = zone_dataset_lookup(zds, dataset, dsnamelen);
	if (zds == NULL || zd == NULL) {
		mutex_exit(&zone_datasets_lock);
		return (ENOENT);
	}

	list_del(&zd->zd_list);
	kmem_free(zd, sizeof (*zd) + zd->zd_dsnamelen + 1);

	/* Prune the namespace entry if it has no more delegations. */
	if (list_empty(&zds->zds_datasets)) {
		/*
		 * Decrease the refcount now that the namespace is no longer
		 * used. It is no longer necessary to prevent the namespace ID
		 * from being reused.
		 */
		put_user_ns(userns);
		list_del(&zds->zds_list);
		kmem_free(zds, sizeof (*zds));
	}

	mutex_exit(&zone_datasets_lock);
	return (0);
#else
	return (ENXIO);
#endif /* defined(CONFIG_USER_NS) && defined(HAVE_USER_NS_COMMON_INUM) */
}
EXPORT_SYMBOL(zone_dataset_detach);

/*
 * A dataset is visible if:
 * - It is a parent of a namespace entry.
 * - It is one of the namespace entries.
 * - It is a child of a namespace entry.
 *
 * A dataset is writable if:
 * - It is one of the namespace entries.
 * - It is a child of a namespace entry.
 *
 * The parent datasets of namespace entries are visible and
 * read-only to provide a path back to the root of the pool.
 */
int
zone_dataset_visible(const char *dataset, int *write)
{
	zone_datasets_t *zds;
	zone_dataset_t *zd;
	size_t dsnamelen, zd_len;
	int visible;

	/* Default to read-only, in case visible is returned. */
	if (write != NULL)
		*write = 0;
	if (zone_dataset_name_check(dataset, &dsnamelen) != 0)
		return (0);
	if (INGLOBALZONE(curproc)) {
		if (write != NULL)
			*write = 1;
		return (1);
	}

	mutex_enter(&zone_datasets_lock);
	zds = zone_datasets_lookup(crgetzoneid(curproc->cred));
	if (zds == NULL) {
		mutex_exit(&zone_datasets_lock);
		return (0);
	}

	visible = 0;
	list_for_each_entry(zd, &zds->zds_datasets, zd_list) {
		zd_len = strlen(zd->zd_dsname);
		if (zd_len > dsnamelen) {
			/*
			 * The name of the namespace entry is longer than that
			 * of the dataset, so it could be that the dataset is a
			 * parent of the namespace entry.
			 */
			visible = memcmp(zd->zd_dsname, dataset,
			    dsnamelen) == 0 &&
			    zd->zd_dsname[dsnamelen] == '/';
			if (visible)
				break;
		} else if (zd_len == dsnamelen) {
			/*
			 * The name of the namespace entry is as long as that
			 * of the dataset, so perhaps the dataset itself is the
			 * namespace entry.
			 */
			visible = memcmp(zd->zd_dsname, dataset, zd_len) == 0;
			if (visible) {
				if (write != NULL)
					*write = 1;
				break;
			}
		} else {
			/*
			 * The name of the namespace entry is shorter than that
			 * of the dataset, so perhaps the dataset is a child of
			 * the namespace entry.
			 */
			visible = memcmp(zd->zd_dsname, dataset,
			    zd_len) == 0 && dataset[zd_len] == '/';
			if (visible) {
				if (write != NULL)
					*write = 1;
				break;
			}
		}
	}

	mutex_exit(&zone_datasets_lock);
	return (visible);
}
EXPORT_SYMBOL(zone_dataset_visible);

unsigned int
global_zoneid(void)
{
	unsigned int z = 0;

#if defined(CONFIG_USER_NS)
	z = user_ns_zoneid(&init_user_ns);
#endif

	return (z);
}
EXPORT_SYMBOL(global_zoneid);

unsigned int
crgetzoneid(const cred_t *cr)
{
	unsigned int r = 0;

#if defined(CONFIG_USER_NS)
	r = user_ns_zoneid(cr->user_ns);
#endif

	return (r);
}
EXPORT_SYMBOL(crgetzoneid);

boolean_t
inglobalzone(proc_t *proc)
{
#if defined(CONFIG_USER_NS)
	return (proc->cred->user_ns == &init_user_ns);
#else
	return (B_TRUE);
#endif
}
EXPORT_SYMBOL(inglobalzone);

int
spl_zone_init(void)
{
	mutex_init(&zone_datasets_lock, NULL, MUTEX_DEFAULT, NULL);
	INIT_LIST_HEAD(&zone_datasets);
	return (0);
}

void
spl_zone_fini(void)
{
	zone_datasets_t *zds;
	zone_dataset_t *zd;

	/*
	 * It would be better to assert an empty zone_datasets, but since
	 * there's no automatic mechanism for cleaning them up if the user
	 * namespace is destroyed, just do it here, since spl is about to go
	 * out of context.
	 */
	while (!list_empty(&zone_datasets)) {
		zds = list_entry(zone_datasets.next, zone_datasets_t, zds_list);
		while (!list_empty(&zds->zds_datasets)) {
			zd = list_entry(zds->zds_datasets.next,
			    zone_dataset_t, zd_list);
			list_del(&zd->zd_list);
			kmem_free(zd, sizeof (*zd) + zd->zd_dsnamelen + 1);
			put_user_ns(zds->zds_userns);
		}
		list_del(&zds->zds_list);
		kmem_free(zds, sizeof (*zds));
	}
	mutex_destroy(&zone_datasets_lock);
}
