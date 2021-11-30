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
/*
 * Copyright (c) 2018, 2019 by Delphix. All rights reserved.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/zfeature.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_sysfs.h>
#include <sys/kmem.h>
#include <sys/fs/zfs.h>
#include <linux/kobject.h>

#include "zfs_prop.h"

#if !defined(_KERNEL)
#error kernel builds only
#endif

/*
 * ZFS Module sysfs support
 *
 * This extends our sysfs '/sys/module/zfs' entry to include feature
 * and property attributes. The primary consumer of this information
 * is user processes, like the zfs CLI, that need to know what the
 * current loaded ZFS module supports. The libzfs binary will consult
 * this information when instantiating the zfs|zpool property tables
 * and the pool features table.
 *
 * The added top-level directories are:
 * /sys/module/zfs
 *		├── features.kernel
 *		├── features.pool
 *		├── properties.dataset
 *		└── properties.pool
 *
 * The local interface for the zfs kobjects includes:
 *	zfs_kobj_init()
 *	zfs_kobj_add()
 *	zfs_kobj_release()
 *	zfs_kobj_add_attr()
 *	zfs_kobj_fini()
 */

/*
 * A zfs_mod_kobj_t represents a zfs kobject under '/sys/module/zfs'
 */
struct zfs_mod_kobj;
typedef struct zfs_mod_kobj zfs_mod_kobj_t;

struct zfs_mod_kobj {
	struct kobject		zko_kobj;
	struct kobj_type	zko_kobj_type;
	struct sysfs_ops	zko_sysfs_ops;
	size_t			zko_attr_count;
	struct attribute	*zko_attr_list;		/* allocated */
	struct attribute	**zko_default_attrs;	/* allocated */
	size_t			zko_child_count;
	zfs_mod_kobj_t		*zko_children;		/* allocated */
};

#define	ATTR_TABLE_SIZE(cnt)	(sizeof (struct attribute) * (cnt))
/* Note +1 for NULL terminator slot */
#define	DEFAULT_ATTR_SIZE(cnt)	(sizeof (struct attribute *) * (cnt + 1))
#define	CHILD_TABLE_SIZE(cnt)	(sizeof (zfs_mod_kobj_t) * (cnt))

/*
 * These are the top-level kobjects under '/sys/module/zfs/'
 */
static zfs_mod_kobj_t kernel_features_kobj;
static zfs_mod_kobj_t pool_features_kobj;
static zfs_mod_kobj_t dataset_props_kobj;
static zfs_mod_kobj_t vdev_props_kobj;
static zfs_mod_kobj_t pool_props_kobj;

/*
 * The show function is used to provide the content
 * of an attribute into a PAGE_SIZE buffer.
 */
typedef ssize_t	(*sysfs_show_func)(struct kobject *, struct attribute *,
    char *);

static void
zfs_kobj_fini(zfs_mod_kobj_t *zkobj)
{
	/* finalize any child kobjects */
	if (zkobj->zko_child_count != 0) {
		ASSERT(zkobj->zko_children);
		for (int i = 0; i < zkobj->zko_child_count; i++)
			zfs_kobj_fini(&zkobj->zko_children[i]);
	}

	/* kobject_put() will call zfs_kobj_release() to release memory */
	kobject_del(&zkobj->zko_kobj);
	kobject_put(&zkobj->zko_kobj);
}

static void
zfs_kobj_release(struct kobject *kobj)
{
	zfs_mod_kobj_t *zkobj = container_of(kobj, zfs_mod_kobj_t, zko_kobj);

	if (zkobj->zko_attr_list != NULL) {
		ASSERT3S(zkobj->zko_attr_count, !=, 0);
		kmem_free(zkobj->zko_attr_list,
		    ATTR_TABLE_SIZE(zkobj->zko_attr_count));
		zkobj->zko_attr_list = NULL;
	}

	if (zkobj->zko_default_attrs != NULL) {
		kmem_free(zkobj->zko_default_attrs,
		    DEFAULT_ATTR_SIZE(zkobj->zko_attr_count));
		zkobj->zko_default_attrs = NULL;
	}

	if (zkobj->zko_child_count != 0) {
		ASSERT(zkobj->zko_children);

		kmem_free(zkobj->zko_children,
		    CHILD_TABLE_SIZE(zkobj->zko_child_count));
		zkobj->zko_child_count = 0;
		zkobj->zko_children = NULL;
	}

	zkobj->zko_attr_count = 0;
}

#ifndef sysfs_attr_init
#define	sysfs_attr_init(attr) do {} while (0)
#endif

static void
zfs_kobj_add_attr(zfs_mod_kobj_t *zkobj, int attr_num, const char *attr_name)
{
	VERIFY3U(attr_num, <, zkobj->zko_attr_count);
	ASSERT(zkobj->zko_attr_list);
	ASSERT(zkobj->zko_default_attrs);

	zkobj->zko_attr_list[attr_num].name = attr_name;
	zkobj->zko_attr_list[attr_num].mode = 0444;
	zkobj->zko_default_attrs[attr_num] = &zkobj->zko_attr_list[attr_num];
	sysfs_attr_init(&zkobj->zko_attr_list[attr_num]);
}

static int
zfs_kobj_init(zfs_mod_kobj_t *zkobj, int attr_cnt, int child_cnt,
    sysfs_show_func show_func)
{
	/*
	 * Initialize object's attributes. Count can be zero.
	 */
	if (attr_cnt > 0) {
		zkobj->zko_attr_list = kmem_zalloc(ATTR_TABLE_SIZE(attr_cnt),
		    KM_SLEEP);
		if (zkobj->zko_attr_list == NULL)
			return (ENOMEM);
	}
	/* this will always have at least one slot for NULL termination */
	zkobj->zko_default_attrs = kmem_zalloc(DEFAULT_ATTR_SIZE(attr_cnt),
	    KM_SLEEP);
	if (zkobj->zko_default_attrs == NULL) {
		if (zkobj->zko_attr_list != NULL) {
			kmem_free(zkobj->zko_attr_list,
			    ATTR_TABLE_SIZE(attr_cnt));
		}
		return (ENOMEM);
	}
	zkobj->zko_attr_count = attr_cnt;
	zkobj->zko_kobj_type.default_attrs = zkobj->zko_default_attrs;

	if (child_cnt > 0) {
		zkobj->zko_children = kmem_zalloc(CHILD_TABLE_SIZE(child_cnt),
		    KM_SLEEP);
		if (zkobj->zko_children == NULL) {
			if (zkobj->zko_default_attrs != NULL) {
				kmem_free(zkobj->zko_default_attrs,
				    DEFAULT_ATTR_SIZE(attr_cnt));
			}
			if (zkobj->zko_attr_list != NULL) {
				kmem_free(zkobj->zko_attr_list,
				    ATTR_TABLE_SIZE(attr_cnt));
			}
			return (ENOMEM);
		}
		zkobj->zko_child_count = child_cnt;
	}

	zkobj->zko_sysfs_ops.show = show_func;
	zkobj->zko_kobj_type.sysfs_ops = &zkobj->zko_sysfs_ops;
	zkobj->zko_kobj_type.release = zfs_kobj_release;

	return (0);
}

static int
zfs_kobj_add(zfs_mod_kobj_t *zkobj, struct kobject *parent, const char *name)
{
	/* zko_default_attrs must be NULL terminated */
	ASSERT(zkobj->zko_default_attrs != NULL);
	ASSERT(zkobj->zko_default_attrs[zkobj->zko_attr_count] == NULL);

	kobject_init(&zkobj->zko_kobj, &zkobj->zko_kobj_type);
	return (kobject_add(&zkobj->zko_kobj, parent, name));
}

/*
 * Each zfs property has these common attributes
 */
static const char *zprop_attrs[]  = {
	"type",
	"readonly",
	"setonce",
	"visible",
	"values",
	"default",
	"datasets"	/* zfs properties only */
};

#define	ZFS_PROP_ATTR_COUNT	ARRAY_SIZE(zprop_attrs)
#define	ZPOOL_PROP_ATTR_COUNT	(ZFS_PROP_ATTR_COUNT - 1)

static const char *zprop_types[]  = {
	"number",
	"string",
	"index",
};

typedef struct zfs_type_map {
	zfs_type_t	ztm_type;
	const char	*ztm_name;
} zfs_type_map_t;

static zfs_type_map_t type_map[] = {
	{ZFS_TYPE_FILESYSTEM,	"filesystem"},
	{ZFS_TYPE_SNAPSHOT,	"snapshot"},
	{ZFS_TYPE_VOLUME,	"volume"},
	{ZFS_TYPE_BOOKMARK,	"bookmark"}
};

/*
 * Show the content for a zfs property attribute
 */
static ssize_t
zprop_sysfs_show(const char *attr_name, const zprop_desc_t *property,
    char *buf, size_t buflen)
{
	const char *show_str;
	char number[32];

	/* For dataset properties list the dataset types that apply */
	if (strcmp(attr_name, "datasets") == 0 &&
	    property->pd_types != ZFS_TYPE_POOL) {
		int len = 0;

		for (int i = 0; i < ARRAY_SIZE(type_map); i++) {
			if (type_map[i].ztm_type & property->pd_types)  {
				len += snprintf(buf + len, buflen - len, "%s ",
				    type_map[i].ztm_name);
			}
		}
		len += snprintf(buf + len, buflen - len, "\n");
		return (len);
	}

	if (strcmp(attr_name, "type") == 0) {
		show_str = zprop_types[property->pd_proptype];
	} else if (strcmp(attr_name, "readonly") == 0) {
		show_str = property->pd_attr == PROP_READONLY ? "1" : "0";
	} else if (strcmp(attr_name, "setonce") == 0) {
		show_str = property->pd_attr == PROP_ONETIME ? "1" : "0";
	} else if (strcmp(attr_name, "visible") == 0) {
		show_str = property->pd_visible ? "1" : "0";
	} else if (strcmp(attr_name, "values") == 0) {
		show_str = property->pd_values ? property->pd_values : "";
	} else if (strcmp(attr_name, "default") == 0) {
		switch (property->pd_proptype) {
		case PROP_TYPE_NUMBER:
			(void) snprintf(number, sizeof (number), "%llu",
			    (u_longlong_t)property->pd_numdefault);
			show_str = number;
			break;
		case PROP_TYPE_STRING:
			show_str = property->pd_strdefault ?
			    property->pd_strdefault : "";
			break;
		case PROP_TYPE_INDEX:
			if (zprop_index_to_string(property->pd_propnum,
			    property->pd_numdefault, &show_str,
			    property->pd_types) != 0) {
				show_str = "";
			}
			break;
		default:
			return (0);
		}
	} else {
		return (0);
	}

	return (snprintf(buf, buflen, "%s\n", show_str));
}

static ssize_t
dataset_property_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	zfs_prop_t prop = zfs_name_to_prop(kobject_name(kobj));
	zprop_desc_t *prop_tbl = zfs_prop_get_table();
	ssize_t len;

	ASSERT3U(prop, <, ZFS_NUM_PROPS);

	len = zprop_sysfs_show(attr->name, &prop_tbl[prop], buf, PAGE_SIZE);

	return (len);
}

static ssize_t
vdev_property_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	vdev_prop_t prop = vdev_name_to_prop(kobject_name(kobj));
	zprop_desc_t *prop_tbl = vdev_prop_get_table();
	ssize_t len;

	ASSERT3U(prop, <, VDEV_NUM_PROPS);

	len = zprop_sysfs_show(attr->name, &prop_tbl[prop], buf, PAGE_SIZE);

	return (len);
}

static ssize_t
pool_property_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	zpool_prop_t prop = zpool_name_to_prop(kobject_name(kobj));
	zprop_desc_t *prop_tbl = zpool_prop_get_table();
	ssize_t len;

	ASSERT3U(prop, <, ZPOOL_NUM_PROPS);

	len = zprop_sysfs_show(attr->name, &prop_tbl[prop], buf, PAGE_SIZE);

	return (len);
}

/*
 * ZFS kernel feature attributes for '/sys/module/zfs/features.kernel'
 *
 * This list is intended for kernel features that don't have a pool feature
 * association or that extend existing user kernel interfaces.
 *
 * A user process can easily check if the running zfs kernel module
 * supports the new feature.
 */
static const char *zfs_kernel_features[] = {
	/* --> Add new kernel features here */
	"com.delphix:vdev_initialize",
	"org.zfsonlinux:vdev_trim",
	"org.openzfs:l2arc_persistent",
};

#define	KERNEL_FEATURE_COUNT	ARRAY_SIZE(zfs_kernel_features)

static ssize_t
kernel_feature_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	if (strcmp(attr->name, "supported") == 0)
		return (snprintf(buf, PAGE_SIZE, "yes\n"));
	return (0);
}

static void
kernel_feature_to_kobj(zfs_mod_kobj_t *parent, int slot, const char *name)
{
	zfs_mod_kobj_t *zfs_kobj = &parent->zko_children[slot];

	ASSERT3U(slot, <, KERNEL_FEATURE_COUNT);
	ASSERT(name);

	int err = zfs_kobj_init(zfs_kobj, 1, 0, kernel_feature_show);
	if (err)
		return;

	zfs_kobj_add_attr(zfs_kobj, 0, "supported");

	err = zfs_kobj_add(zfs_kobj, &parent->zko_kobj, name);
	if (err)
		zfs_kobj_release(&zfs_kobj->zko_kobj);
}

static int
zfs_kernel_features_init(zfs_mod_kobj_t *zfs_kobj, struct kobject *parent)
{
	/*
	 * Create a parent kobject to host kernel features.
	 *
	 * '/sys/module/zfs/features.kernel'
	 */
	int err = zfs_kobj_init(zfs_kobj, 0, KERNEL_FEATURE_COUNT,
	    kernel_feature_show);
	if (err)
		return (err);
	err = zfs_kobj_add(zfs_kobj, parent, ZFS_SYSFS_KERNEL_FEATURES);
	if (err) {
		zfs_kobj_release(&zfs_kobj->zko_kobj);
		return (err);
	}

	/*
	 * Now create a kobject for each feature.
	 *
	 * '/sys/module/zfs/features.kernel/<feature>'
	 */
	for (int f = 0; f < KERNEL_FEATURE_COUNT; f++)
		kernel_feature_to_kobj(zfs_kobj, f, zfs_kernel_features[f]);

	return (0);
}

/*
 * Each pool feature has these common attributes
 */
static const char *pool_feature_attrs[]  = {
	"description",
	"guid",
	"uname",
	"readonly_compatible",
	"required_for_mos",
	"activate_on_enable",
	"per_dataset"
};

#define	ZPOOL_FEATURE_ATTR_COUNT	ARRAY_SIZE(pool_feature_attrs)

/*
 * Show the content for the given zfs pool feature attribute
 */
static ssize_t
pool_feature_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	spa_feature_t fid;

	if (zfeature_lookup_guid(kobject_name(kobj), &fid) != 0)
		return (0);

	ASSERT3U(fid, <, SPA_FEATURES);

	zfeature_flags_t flags = spa_feature_table[fid].fi_flags;
	const char *show_str = NULL;

	if (strcmp(attr->name, "description") == 0) {
		show_str = spa_feature_table[fid].fi_desc;
	} else if (strcmp(attr->name, "guid") == 0) {
		show_str = spa_feature_table[fid].fi_guid;
	} else if (strcmp(attr->name, "uname") == 0) {
		show_str = spa_feature_table[fid].fi_uname;
	} else if (strcmp(attr->name, "readonly_compatible") == 0) {
		show_str = flags & ZFEATURE_FLAG_READONLY_COMPAT ? "1" : "0";
	} else if (strcmp(attr->name, "required_for_mos") == 0) {
		show_str = flags & ZFEATURE_FLAG_MOS ? "1" : "0";
	} else if (strcmp(attr->name, "activate_on_enable") == 0) {
		show_str = flags & ZFEATURE_FLAG_ACTIVATE_ON_ENABLE ? "1" : "0";
	} else if (strcmp(attr->name, "per_dataset") == 0) {
		show_str = flags & ZFEATURE_FLAG_PER_DATASET ? "1" : "0";
	}
	if (show_str == NULL)
		return (0);

	return (snprintf(buf, PAGE_SIZE, "%s\n", show_str));
}

static void
pool_feature_to_kobj(zfs_mod_kobj_t *parent, spa_feature_t fid,
    const char *name)
{
	zfs_mod_kobj_t *zfs_kobj = &parent->zko_children[fid];

	ASSERT3U(fid, <, SPA_FEATURES);
	ASSERT(name);

	int err = zfs_kobj_init(zfs_kobj, ZPOOL_FEATURE_ATTR_COUNT, 0,
	    pool_feature_show);
	if (err)
		return;

	for (int i = 0; i < ZPOOL_FEATURE_ATTR_COUNT; i++)
		zfs_kobj_add_attr(zfs_kobj, i, pool_feature_attrs[i]);

	err = zfs_kobj_add(zfs_kobj, &parent->zko_kobj, name);
	if (err)
		zfs_kobj_release(&zfs_kobj->zko_kobj);
}

static int
zfs_pool_features_init(zfs_mod_kobj_t *zfs_kobj, struct kobject *parent)
{
	/*
	 * Create a parent kobject to host pool features.
	 *
	 * '/sys/module/zfs/features.pool'
	 */
	int err = zfs_kobj_init(zfs_kobj, 0, SPA_FEATURES, pool_feature_show);
	if (err)
		return (err);
	err = zfs_kobj_add(zfs_kobj, parent, ZFS_SYSFS_POOL_FEATURES);
	if (err) {
		zfs_kobj_release(&zfs_kobj->zko_kobj);
		return (err);
	}

	/*
	 * Now create a kobject for each feature.
	 *
	 * '/sys/module/zfs/features.pool/<feature>'
	 */
	for (spa_feature_t i = 0; i < SPA_FEATURES; i++)
		pool_feature_to_kobj(zfs_kobj, i, spa_feature_table[i].fi_guid);

	return (0);
}

typedef struct prop_to_kobj_arg {
	zprop_desc_t	*p2k_table;
	zfs_mod_kobj_t	*p2k_parent;
	sysfs_show_func	p2k_show_func;
	int		p2k_attr_count;
} prop_to_kobj_arg_t;

static int
zprop_to_kobj(int prop, void *args)
{
	prop_to_kobj_arg_t *data = args;
	zfs_mod_kobj_t *parent = data->p2k_parent;
	zfs_mod_kobj_t *zfs_kobj = &parent->zko_children[prop];
	const char *name = data->p2k_table[prop].pd_name;
	int err;

	ASSERT(name);

	err = zfs_kobj_init(zfs_kobj, data->p2k_attr_count, 0,
	    data->p2k_show_func);
	if (err)
		return (ZPROP_CONT);

	for (int i = 0; i < data->p2k_attr_count; i++)
		zfs_kobj_add_attr(zfs_kobj, i, zprop_attrs[i]);

	err = zfs_kobj_add(zfs_kobj, &parent->zko_kobj, name);
	if (err)
		zfs_kobj_release(&zfs_kobj->zko_kobj);

	return (ZPROP_CONT);
}

static int
zfs_sysfs_properties_init(zfs_mod_kobj_t *zfs_kobj, struct kobject *parent,
    zfs_type_t type)
{
	prop_to_kobj_arg_t context;
	const char *name;
	int err;

	/*
	 * Create a parent kobject to host properties.
	 *
	 * '/sys/module/zfs/properties.<type>'
	 */
	if (type == ZFS_TYPE_POOL) {
		name = ZFS_SYSFS_POOL_PROPERTIES;
		context.p2k_table = zpool_prop_get_table();
		context.p2k_attr_count = ZPOOL_PROP_ATTR_COUNT;
		context.p2k_parent = zfs_kobj;
		context.p2k_show_func = pool_property_show;
		err = zfs_kobj_init(zfs_kobj, 0, ZPOOL_NUM_PROPS,
		    pool_property_show);
	} else if (type == ZFS_TYPE_VDEV) {
		name = ZFS_SYSFS_VDEV_PROPERTIES;
		context.p2k_table = vdev_prop_get_table();
		context.p2k_attr_count = ZPOOL_PROP_ATTR_COUNT;
		context.p2k_parent = zfs_kobj;
		context.p2k_show_func = vdev_property_show;
		err = zfs_kobj_init(zfs_kobj, 0, VDEV_NUM_PROPS,
		    vdev_property_show);
	} else {
		name = ZFS_SYSFS_DATASET_PROPERTIES;
		context.p2k_table = zfs_prop_get_table();
		context.p2k_attr_count = ZFS_PROP_ATTR_COUNT;
		context.p2k_parent = zfs_kobj;
		context.p2k_show_func = dataset_property_show;
		err = zfs_kobj_init(zfs_kobj, 0, ZFS_NUM_PROPS,
		    dataset_property_show);
	}

	if (err)
		return (err);

	err = zfs_kobj_add(zfs_kobj, parent, name);
	if (err) {
		zfs_kobj_release(&zfs_kobj->zko_kobj);
		return (err);
	}

	/*
	 * Create a kobject for each property.
	 *
	 * '/sys/module/zfs/properties.<type>/<property>'
	 */
	(void) zprop_iter_common(zprop_to_kobj, &context, B_TRUE,
	    B_FALSE, type);

	return (err);
}

void
zfs_sysfs_init(void)
{
	struct kobject *parent;
#if defined(CONFIG_ZFS) && !defined(CONFIG_ZFS_MODULE)
	parent = kobject_create_and_add("zfs", fs_kobj);
#else
	parent = &(((struct module *)(THIS_MODULE))->mkobj).kobj;
#endif
	int err;

	if (parent == NULL)
		return;

	err = zfs_kernel_features_init(&kernel_features_kobj, parent);
	if (err)
		return;

	err = zfs_pool_features_init(&pool_features_kobj, parent);
	if (err) {
		zfs_kobj_fini(&kernel_features_kobj);
		return;
	}

	err = zfs_sysfs_properties_init(&pool_props_kobj, parent,
	    ZFS_TYPE_POOL);
	if (err) {
		zfs_kobj_fini(&kernel_features_kobj);
		zfs_kobj_fini(&pool_features_kobj);
		return;
	}

	err = zfs_sysfs_properties_init(&vdev_props_kobj, parent,
	    ZFS_TYPE_VDEV);
	if (err) {
		zfs_kobj_fini(&kernel_features_kobj);
		zfs_kobj_fini(&pool_features_kobj);
		zfs_kobj_fini(&pool_props_kobj);
		return;
	}

	err = zfs_sysfs_properties_init(&dataset_props_kobj, parent,
	    ZFS_TYPE_FILESYSTEM);
	if (err) {
		zfs_kobj_fini(&kernel_features_kobj);
		zfs_kobj_fini(&pool_features_kobj);
		zfs_kobj_fini(&pool_props_kobj);
		zfs_kobj_fini(&vdev_props_kobj);
		return;
	}
}

void
zfs_sysfs_fini(void)
{
	/*
	 * Remove top-level kobjects; each will remove any children kobjects
	 */
	zfs_kobj_fini(&kernel_features_kobj);
	zfs_kobj_fini(&pool_features_kobj);
	zfs_kobj_fini(&pool_props_kobj);
	zfs_kobj_fini(&vdev_props_kobj);
	zfs_kobj_fini(&dataset_props_kobj);
}
