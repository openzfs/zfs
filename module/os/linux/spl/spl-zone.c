// SPDX-License-Identifier: BSD-2-Clause
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

/*
 * Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/param.h>
#include <linux/file.h>
#include <linux/magic.h>
#include <sys/zone.h>
#include <sys/string.h>

#if defined(CONFIG_USER_NS)
#include <linux/statfs.h>
#include <linux/proc_ns.h>
#endif

#include <sys/mutex.h>

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
	char zd_dsname[];		/* name of the member dataset */
} zone_dataset_t;

typedef struct zone_uid_dataset {
	struct list_head zud_list;	/* zone_uid_datasets linkage */
	uid_t zud_uid;			/* directly configured UID */
	size_t zud_dsnamelen;		/* length of name */
	char zud_dsname[];		/* local or received setpoint */
} zone_uid_dataset_t;

static struct list_head zone_uid_datasets;

#ifdef CONFIG_USER_NS

/*
 * Linux 6.18 moved the generic namespace type away from ns->ops->type onto
 * ns_common itself.
 */
#ifdef HAVE_NS_COMMON_TYPE
#define	ns_is_newuser(ns)	\
	((ns)->ns_type == CLONE_NEWUSER)
#else
#define	ns_is_newuser(ns)	\
	((ns)->ops != NULL && (ns)->ops->type == CLONE_NEWUSER)
#endif

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
	if (!ns_is_newuser(ns)) {
		error = ENOTTY;
		goto done;
	}
	*userns = container_of(ns, struct user_namespace, ns);

	error = 0;
done:
	fput(nsfile);

	return (error);
}
#endif /* CONFIG_USER_NS */

static unsigned int
user_ns_zoneid(struct user_namespace *user_ns)
{
	unsigned int r;

	r = user_ns->ns.inum;

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

#ifdef CONFIG_USER_NS
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

static zone_uid_dataset_t *
zone_dataset_lookup_uid(const char *dataset, size_t dsnamelen)
{
	zone_uid_dataset_t *zud;

	list_for_each_entry(zud, &zone_uid_datasets, zud_list) {
		if (zud->zud_dsnamelen == dsnamelen &&
		    memcmp(zud->zud_dsname, dataset, dsnamelen) == 0)
			return (zud);
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
#endif /* CONFIG_USER_NS */

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

#ifdef CONFIG_USER_NS
static boolean_t
zone_dataset_name_contains(const char *root, size_t rootlen,
    const char *dataset, size_t dsnamelen)
{
	if (rootlen > dsnamelen || memcmp(root, dataset, rootlen) != 0)
		return (B_FALSE);
	if (rootlen == dsnamelen)
		return (B_TRUE);
	return (dataset[rootlen] == '/' || dataset[rootlen] == '@' ||
	    dataset[rootlen] == '#');
}

static boolean_t
zone_dataset_name_is_parent(const char *parent, size_t parentlen,
    const char *dataset, size_t dsnamelen)
{
	return (parentlen < dsnamelen &&
	    memcmp(parent, dataset, parentlen) == 0 &&
	    dataset[parentlen] == '/');
}
#endif

int
zone_dataset_attach(cred_t *cred, const char *dataset, int userns_fd)
{
#ifdef CONFIG_USER_NS
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
	strlcpy(zd->zd_dsname, dataset, dsnamelen + 1);
	INIT_LIST_HEAD(&zd->zd_list);
	list_add_tail(&zd->zd_list, &zds->zds_datasets);

	mutex_exit(&zone_datasets_lock);
	return (0);
#else
	return (ENXIO);
#endif /* CONFIG_USER_NS */
}
EXPORT_SYMBOL(zone_dataset_attach);

int
zone_dataset_uid_check(cred_t *cred, uint64_t owner_uid)
{
#ifdef CONFIG_USER_NS
	int error;

	if ((error = zone_dataset_cred_check(cred)) != 0)
		return (error);
	if (owner_uid >= MAXUID)
		return (EINVAL);
	return (uid_valid(make_kuid(&init_user_ns, (uid_t)owner_uid)) ?
	    0 : EINVAL);
#else
	return (ENXIO);
#endif
}
EXPORT_SYMBOL(zone_dataset_uid_check);

void
zone_dataset_set_uid(const char *dataset, uint64_t owner_uid)
{
#ifdef CONFIG_USER_NS
	zone_uid_dataset_t *new_zud;
	zone_uid_dataset_t *zud;
	size_t dsnamelen = strlen(dataset);

	/* Invalid stored values must not expose ancestor grants. */
	if (owner_uid >= MAXUID)
		owner_uid = 0;
	new_zud = kmem_alloc(sizeof (*new_zud) + dsnamelen + 1, KM_SLEEP);
	new_zud->zud_uid = (uid_t)owner_uid;
	new_zud->zud_dsnamelen = dsnamelen;
	strlcpy(new_zud->zud_dsname, dataset, dsnamelen + 1);
	INIT_LIST_HEAD(&new_zud->zud_list);

	mutex_enter(&zone_datasets_lock);
	zud = zone_dataset_lookup_uid(dataset, dsnamelen);
	if (zud != NULL) {
		zud->zud_uid = (uid_t)owner_uid;
		mutex_exit(&zone_datasets_lock);
		kmem_free(new_zud, sizeof (*new_zud) + dsnamelen + 1);
		return;
	}
	list_add_tail(&new_zud->zud_list, &zone_uid_datasets);
	mutex_exit(&zone_datasets_lock);
#endif /* CONFIG_USER_NS */
}
EXPORT_SYMBOL(zone_dataset_set_uid);

int
zone_dataset_detach(cred_t *cred, const char *dataset, int userns_fd)
{
#ifdef CONFIG_USER_NS
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
#endif /* CONFIG_USER_NS */
}
EXPORT_SYMBOL(zone_dataset_detach);

void
zone_dataset_unset_uid(const char *dataset)
{
#ifdef CONFIG_USER_NS
	zone_uid_dataset_t *zud;
	size_t dsnamelen = strlen(dataset);

	mutex_enter(&zone_datasets_lock);
	zud = zone_dataset_lookup_uid(dataset, dsnamelen);
	if (zud == NULL) {
		mutex_exit(&zone_datasets_lock);
		return;
	}
	list_del(&zud->zud_list);
	mutex_exit(&zone_datasets_lock);

	kmem_free(zud, sizeof (*zud) + zud->zud_dsnamelen + 1);
#endif /* CONFIG_USER_NS */
}
EXPORT_SYMBOL(zone_dataset_unset_uid);

void
zone_dataset_remove_uid(const char *dataset)
{
#ifdef CONFIG_USER_NS
	zone_uid_dataset_t *next;
	zone_uid_dataset_t *zud;
	size_t dsnamelen = strlen(dataset);

	mutex_enter(&zone_datasets_lock);
	list_for_each_entry_safe(zud, next, &zone_uid_datasets, zud_list) {
		if (!zone_dataset_name_contains(dataset, dsnamelen,
		    zud->zud_dsname, zud->zud_dsnamelen))
			continue;
		list_del(&zud->zud_list);
		kmem_free(zud, sizeof (*zud) + zud->zud_dsnamelen + 1);
	}
	mutex_exit(&zone_datasets_lock);
#endif /* CONFIG_USER_NS */
}
EXPORT_SYMBOL(zone_dataset_remove_uid);

void
zone_dataset_rename_uid(const char *from, const char *to)
{
#ifdef CONFIG_USER_NS
	zone_uid_dataset_t *next;
	zone_uid_dataset_t *new_zud;
	zone_uid_dataset_t *zud;
	size_t fromlen = strlen(from);
	size_t suffixlen;
	size_t tolen = strlen(to);

	mutex_enter(&zone_datasets_lock);
	list_for_each_entry_safe(zud, next, &zone_uid_datasets, zud_list) {
		if (!zone_dataset_name_contains(from, fromlen,
		    zud->zud_dsname, zud->zud_dsnamelen))
			continue;

		suffixlen = zud->zud_dsnamelen - fromlen;
		new_zud = kmem_alloc(sizeof (*new_zud) + tolen + suffixlen + 1,
		    KM_SLEEP);
		new_zud->zud_uid = zud->zud_uid;
		new_zud->zud_dsnamelen = tolen + suffixlen;
		memcpy(new_zud->zud_dsname, to, tolen);
		memcpy(new_zud->zud_dsname + tolen,
		    zud->zud_dsname + fromlen, suffixlen + 1);
		list_replace(&zud->zud_list, &new_zud->zud_list);
		kmem_free(zud, sizeof (*zud) + zud->zud_dsnamelen + 1);
	}
	mutex_exit(&zone_datasets_lock);
#endif /* CONFIG_USER_NS */
}
EXPORT_SYMBOL(zone_dataset_rename_uid);

/*
 * Callback for looking up zoned_uid property (registered by ZFS module).
 */
static zone_get_zoned_uid_fn_t zone_get_zoned_uid_fn = NULL;

void
zone_register_zoned_uid_callback(zone_get_zoned_uid_fn_t fn)
{
	zone_get_zoned_uid_fn = fn;
}
EXPORT_SYMBOL(zone_register_zoned_uid_callback);

void
zone_unregister_zoned_uid_callback(void)
{
	zone_get_zoned_uid_fn = NULL;
}
EXPORT_SYMBOL(zone_unregister_zoned_uid_callback);

#ifdef CONFIG_USER_NS
/*
 * Check if a dataset is the delegation root (has zoned_uid set locally).
 */
static boolean_t
zone_dataset_is_zoned_uid_root(const char *dataset, uid_t zoned_uid)
{
	char *root;
	uid_t found_uid;
	boolean_t is_root;

	if (zone_get_zoned_uid_fn == NULL)
		return (B_FALSE);

	root = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	found_uid = zone_get_zoned_uid_fn(dataset, root, MAXPATHLEN);
	is_root = (found_uid == zoned_uid && strcmp(root, dataset) == 0);
	kmem_free(root, MAXPATHLEN);
	return (is_root);
}
#endif /* CONFIG_USER_NS */

/*
 * Core authorization check for zoned_uid write delegation.
 */
zone_admin_result_t
zone_dataset_admin_check(const char *dataset, zone_uid_op_t op,
    const char *aux_dataset)
{
#ifdef CONFIG_USER_NS
	struct user_namespace *user_ns;
	char *delegation_root;
	uid_t zoned_uid, ns_owner_uid;
	int write_unused;
	zone_admin_result_t result = ZONE_ADMIN_NOT_APPLICABLE;

	/* Step 1: If in global zone, not applicable */
	if (INGLOBALZONE(curproc))
		return (ZONE_ADMIN_NOT_APPLICABLE);

	/* Step 2: Need callback to be registered */
	if (zone_get_zoned_uid_fn == NULL)
		return (ZONE_ADMIN_NOT_APPLICABLE);

	delegation_root = kmem_alloc(MAXPATHLEN, KM_SLEEP);

	/* Step 3: Find delegation root */
	zoned_uid = zone_get_zoned_uid_fn(dataset, delegation_root,
	    MAXPATHLEN);
	if (zoned_uid == 0)
		goto out;

	/* Step 4: Verify namespace owner matches */
	user_ns = current_user_ns();
	ns_owner_uid = from_kuid(&init_user_ns, user_ns->owner);
	if (ns_owner_uid != zoned_uid)
		goto out;

	/* Step 5: Tiered capability check based on operation class */
	{
		int required_cap;
		switch (op) {
		case ZONE_OP_DESTROY:
		case ZONE_OP_RENAME:
		case ZONE_OP_CLONE:
			required_cap = CAP_SYS_ADMIN;
			break;
		case ZONE_OP_CREATE:
		case ZONE_OP_SNAPSHOT:
		case ZONE_OP_SETPROP:
			required_cap = CAP_FOWNER;
			break;
		default:
			required_cap = CAP_SYS_ADMIN;
			break;
		}
		if (!ns_capable(user_ns, required_cap)) {
			result = ZONE_ADMIN_DENIED;
			goto out;
		}
	}

	/* Step 6: Operation-specific constraints */
	switch (op) {
	case ZONE_OP_DESTROY:
		/* Cannot destroy the delegation root itself */
		if (zone_dataset_is_zoned_uid_root(dataset, zoned_uid)) {
			result = ZONE_ADMIN_DENIED;
			goto out;
		}
		break;

	case ZONE_OP_RENAME:
		/* Cannot rename outside delegation subtree */
		if (aux_dataset != NULL) {
			char *dst_root;
			uid_t dst_uid;

			dst_root = kmem_alloc(MAXPATHLEN, KM_SLEEP);
			dst_uid = zone_get_zoned_uid_fn(aux_dataset,
			    dst_root, MAXPATHLEN);
			if (dst_uid != zoned_uid ||
			    strcmp(dst_root, delegation_root) != 0) {
				kmem_free(dst_root, MAXPATHLEN);
				result = ZONE_ADMIN_DENIED;
				goto out;
			}
			kmem_free(dst_root, MAXPATHLEN);
		}
		break;

	case ZONE_OP_CLONE:
		/* Clone source must be visible */
		if (aux_dataset != NULL) {
			if (!zone_dataset_visible(aux_dataset, &write_unused)) {
				result = ZONE_ADMIN_DENIED;
				goto out;
			}
		}
		break;

	case ZONE_OP_CREATE:
	case ZONE_OP_SNAPSHOT:
	case ZONE_OP_SETPROP:
		/* No additional constraints */
		break;
	}

	result = ZONE_ADMIN_ALLOWED;
out:
	kmem_free(delegation_root, MAXPATHLEN);
	return (result);
#else
	(void) dataset, (void) op, (void) aux_dataset;
	return (ZONE_ADMIN_NOT_APPLICABLE);
#endif
}
EXPORT_SYMBOL(zone_dataset_admin_check);

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
/*
 * Helper function to check if a dataset matches against a list of
 * delegated datasets. Returns visibility and sets write permission.
 */
static int
zone_dataset_check_list(struct list_head *datasets, const char *dataset,
    size_t dsnamelen, int *write)
{
	zone_dataset_t *zd;
	size_t zd_len;
	int visible = 0;

	list_for_each_entry(zd, datasets, zd_list) {
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
			    zd_len) == 0 && (dataset[zd_len] == '/' ||
			    dataset[zd_len] == '@' || dataset[zd_len] == '#');
			if (visible) {
				if (write != NULL)
					*write = 1;
				break;
			}
		}
	}

	return (visible);
}

#if defined(CONFIG_USER_NS)
/*
 * Check UID-based zoning visibility for the current process.
 * Must be called with zone_datasets_lock held.
 */
static int
zone_dataset_visible_uid(const char *dataset, size_t dsnamelen, int *write)
{
	zone_uid_dataset_t *best = NULL;
	zone_uid_dataset_t *zud;
	uid_t owner;

	owner = from_kuid(&init_user_ns, current_user_ns()->owner);
	list_for_each_entry(zud, &zone_uid_datasets, zud_list) {
		if (zone_dataset_name_contains(zud->zud_dsname,
		    zud->zud_dsnamelen, dataset, dsnamelen) &&
		    (best == NULL ||
		    zud->zud_dsnamelen > best->zud_dsnamelen))
			best = zud;
	}
	if (best != NULL && best->zud_uid != 0 && best->zud_uid == owner) {
		if (write != NULL)
			*write = 1;
		return (1);
	}

	list_for_each_entry(zud, &zone_uid_datasets, zud_list) {
		if (zud->zud_uid != 0 && zud->zud_uid == owner &&
		    zone_dataset_name_is_parent(dataset, dsnamelen,
		    zud->zud_dsname, zud->zud_dsnamelen))
			return (1);
	}
	return (0);
}
#endif

int
zone_dataset_visible(const char *dataset, int *write)
{
	zone_datasets_t *zds;
	size_t dsnamelen;
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

	/* First, check namespace-specific zoning (existing behavior) */
	zds = zone_datasets_lookup(crgetzoneid(curproc->cred));
	if (zds != NULL) {
		visible = zone_dataset_check_list(&zds->zds_datasets, dataset,
		    dsnamelen, write);
		if (visible) {
			mutex_exit(&zone_datasets_lock);
			return (visible);
		}
	}

	/* Second, check UID-based zoning */
#if defined(CONFIG_USER_NS)
	visible = zone_dataset_visible_uid(dataset, dsnamelen, write);
	if (visible) {
		mutex_exit(&zone_datasets_lock);
		return (visible);
	}
#endif

	mutex_exit(&zone_datasets_lock);
	return (0);
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
	(void) proc;
#if defined(CONFIG_USER_NS)
	return (current_user_ns() == &init_user_ns);
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
	INIT_LIST_HEAD(&zone_uid_datasets);
	return (0);
}

void
spl_zone_fini(void)
{
	zone_datasets_t *zds;
	zone_uid_dataset_t *zud;
	zone_dataset_t *zd;

	/*
	 * It would be better to assert an empty zone_datasets, but since
	 * there's no automatic mechanism for cleaning them up if the user
	 * namespace is destroyed, just do it here, since spl is about to go
	 * out of context.
	 */

	/* Clean up UID-based delegations */
	while (!list_empty(&zone_uid_datasets)) {
		zud = list_entry(zone_uid_datasets.next,
		    zone_uid_dataset_t, zud_list);
		list_del(&zud->zud_list);
		kmem_free(zud, sizeof (*zud) + zud->zud_dsnamelen + 1);
	}

	/* Clean up namespace-based delegations */
	while (!list_empty(&zone_datasets)) {
		zds = list_entry(zone_datasets.next, zone_datasets_t, zds_list);
		while (!list_empty(&zds->zds_datasets)) {
			zd = list_entry(zds->zds_datasets.next,
			    zone_dataset_t, zd_list);
			list_del(&zd->zd_list);
			kmem_free(zd, sizeof (*zd) + zd->zd_dsnamelen + 1);
		}
		put_user_ns(zds->zds_userns);
		list_del(&zds->zds_list);
		kmem_free(zds, sizeof (*zds));
	}
	mutex_destroy(&zone_datasets_lock);
}
