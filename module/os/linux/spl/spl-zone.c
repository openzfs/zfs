/*
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
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
 */

#include <sys/proc.h>
#include <sys/time.h>
#include <sys/zone.h>
#include <sys/zfs_vfsops.h>
#include <linux/idr.h>
#include <linux/btree.h>
#include <linux/parser.h>
#include <linux/string.h>
#include <linux/time.h>

static int __parse_zfs(zone_t *zone, char *kbuf, size_t buflen);
static void zone_kstat_create(zone_t *);
static void zone_kstat_delete(zone_t *);
static void spl_zuc_flush(zone_t *match);
static zone_userns_cache_list_t *spl_zuc_lookup_by_userns_impl(
    struct user_namespace *);
static void spl_zuc_insert_impl(zone_t *,
    struct user_namespace *);
static zone_t *spl_getzone_by_userns_impl(struct user_namespace *userns,
    bool cache_result);
ssize_t proc_zone_write(struct file *file, const char __user *buf,
    size_t cmdlen, loff_t *offset);
static const kstat_proc_op_t proc_curzone_operations;
static const kstat_proc_op_t proc_zone_operations;
static const kstat_proc_op_t proc_zone_datasets_operations;
static struct proc_dir_entry *proc_spl_zone = NULL;

static kmutex_t zonehash_lock;
DEFINE_IDR(zoneid_space);		/* Protected by zonehash_lock */
struct btree_head64 zone_by_userns;	/* Protected by zonehash_lock */
list_t zone_userns_cache_list;		/* Protected by separate lock: */
static kmutex_t zone_userns_cache_list_lock;

/*
 * "zone_pdata" is an array indexed by zoneid. It is used to store "persistent"
 * data which can be referenced independently of the zone_t structure.
 */
zone_persist_t zone_pdata[MAX_ZONES];
EXPORT_SYMBOL(zone_pdata);

zone_t zone0;
zone_zfs_io_t zone0_zp_zfs;
zone_t *global_zone = NULL;	/* Set when the global zone is initialized */

/*
 * list of active zones, protected by zonehash_lock.
 */
static list_t zone_active;

zone_t *
currentzone(void)
{
	zone_t *zone;

	mutex_enter(&zonehash_lock);
	zone = spl_getzone_by_userns_impl(current_user_ns(), true);
	mutex_exit(&zonehash_lock);

	if (!zone)
		return &zone0;

	return zone;
}
EXPORT_SYMBOL(currentzone);

static inline zoneid_t
getzoneid_by_userns(struct user_namespace *userns)
{
	zone_t *zone;

	mutex_enter(&zonehash_lock);
	zone = spl_getzone_by_userns_impl(userns, true);
	mutex_exit(&zonehash_lock);

	if (zone == NULL)
		return GLOBAL_ZONEID;
	else
		return zone->zone_id;
}

zoneid_t
getzoneid(void)
{
	return getzoneid_by_userns(current_user_ns());
}
EXPORT_SYMBOL(getzoneid);

zoneid_t
getzoneid_task(struct task_struct *task)
{
	return getzoneid_by_userns(task_cred_xxx(task, user_ns));
}
EXPORT_SYMBOL(getzoneid_task);

zoneid_t
crgetzoneid(cred_t *cred)
{
	return getzoneid_by_userns(cred->user_ns);
}
EXPORT_SYMBOL(crgetzoneid);

zone_t *
spl_zone_find_by_id(zoneid_t zoneid)
{
	return idr_find(&zoneid_space, zoneid);
}
EXPORT_SYMBOL(spl_zone_find_by_id);

static inline zone_status_t
__zone_status_get(zone_t *zone)
{
	if (!zone)
		return ZONE_IS_FREE;
	return zone->zone_status;
}

zone_status_t
zone_status_get(zone_t *zone)
{
	zone_status_t ret = ZONE_IS_UNINITIALIZED;

	if (!zone)
		return ZONE_IS_FREE;

	mutex_enter(&zone->zone_lock);
	ret = __zone_status_get(zone);
	mutex_exit(&zone->zone_lock);
	return ret;
}
EXPORT_SYMBOL(zone_status_get);

/*
 * Walk the list of active zones and issue the provided callback for
 * each of them.
 *
 * Caller must not be holding any locks that may be acquired under
 * zonehash_lock.  See comment at the beginning of the file for a list of
 * common locks and their interactions with zones.
 */
int
zone_walk(int (*cb)(zone_t *, void *), void *data)
{
	zone_t *zone;
	int ret = 0;
	zone_status_t status;

	mutex_enter(&zonehash_lock);
	for (zone = list_head(&zone_active); zone != NULL;
	    zone = list_next(&zone_active, zone)) {
		/*
		 * Skip zones that shouldn't be externally visible.
		 */
		status = zone_status_get(zone);
		if (status < ZONE_IS_READY || status > ZONE_IS_DOWN)
			continue;
		/*
		 * Bail immediately if any callback invocation returns a
		 * non-zero value.
		 */
		ret = (*cb)(zone, data);
		if (ret != 0)
			break;
	}
	mutex_exit(&zonehash_lock);
	return (ret);
}
EXPORT_SYMBOL(zone_walk);

/*
 * Frees memory associated with the zone dataset list.
 */
static void
spl_zone_free_datasets(zone_t *zone)
{
	zone_dataset_t *t, *next;

	for (t = list_head(&zone->zone_datasets); t != NULL; t = next) {
		next = list_next(&zone->zone_datasets, t);
		list_remove(&zone->zone_datasets, t);
		kmem_free(t->zd_dataset, strlen(t->zd_dataset) + 1);
		kmem_free(t, sizeof (*t));
		zone->zone_numdatasets--;
	}
	list_destroy(&zone->zone_datasets);
}

void
spl_zone_free(zone_t *zone)
{
	pr_debug("%s: zone: %i\n", __func__, zone->zone_id);

	if (zone->zone_userns)
		put_user_ns(zone->zone_userns);

	remove_proc_entry("datasets", zone->zone_proc);

	char *tmpname = kmem_asprintf("zone_%i", zone->zone_id);
	remove_proc_entry(tmpname, proc_spl);
	kmem_strfree(tmpname);

	mutex_enter(&zonehash_lock);
	mutex_destroy(&zone_pdata[zone->zone_id].zpers_zfs_lock);
	idr_remove(&zoneid_space, zone->zone_id);
	list_remove(&zone_active, zone);
	mutex_exit(&zonehash_lock);
	spl_zuc_flush(zone);
	list_destroy(&zone->zone_userns_cache_list);

	spl_zone_free_datasets(zone);
	zone_kstat_delete(zone);
	kmem_free(zone->zone_name, ZONE_NAMELEN);
	mutex_destroy(&zone->zone_lock);
	kmem_free(zone, sizeof (zone_t));
}
EXPORT_SYMBOL(spl_zone_free);

zoneid_t
spl_zone_create(char *zone_name, char *zfsbuf, size_t zfsbufsz,
    int *extended_error)
{
	int zone_id_alloc_req;
	zone_t *zone;

	zone = kmem_zalloc(sizeof(zone_t), KM_SLEEP);

	mutex_enter(&zonehash_lock);
	zone_id_alloc_req = idr_alloc(&zoneid_space, zone,
	    MIN_USERZONEID, MAX_ZONEID, GFP_KERNEL);
	mutex_exit(&zonehash_lock);

	if (zone_id_alloc_req < MIN_USERZONEID) {
		kmem_free(zone, sizeof (zone_t));
		*extended_error = -ENOSPC;
		return 0;
	}

	/* copy node name */
	zone->zone_name = kmem_zalloc(ZONE_NAMELEN, KM_SLEEP);
	mutex_init(&zone->zone_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_enter(&zone->zone_lock);
	(void) strncpy(zone->zone_name, zone_name, ZONE_NAMELEN);
	zone->zone_name[ZONE_NAMELEN - 1] = '\0';
	zone->zone_hostid = HW_INVALID_HOSTID;
	zone->zone_id = (zoneid_t) zone_id_alloc_req;
	zone->zone_userns = NULL;

	list_create(&zone->zone_userns_cache_list, sizeof (zone_userns_cache_list_t),
	    offsetof(zone_userns_cache_list_t, zuc_node));
	list_create(&zone->zone_datasets, sizeof (zone_dataset_t),
	    offsetof(zone_dataset_t, zd_linkage));

	mutex_init(&zone_pdata[zone->zone_id].zpers_zfs_lock, NULL, MUTEX_DEFAULT, NULL);
	zone_pdata[zone->zone_id].zpers_zfsp =
	    kmem_zalloc(sizeof (zone_zfs_io_t), KM_SLEEP);
	zone_pdata[zone->zone_id].zpers_zfsp->zpers_zfs_io_pri = 1;

	__parse_zfs(zone, zfsbuf, zfsbufsz);

	char *tmpname = kmem_asprintf("zone_%i", zone->zone_id);
	zone->zone_proc = proc_mkdir(tmpname, proc_spl);
	kmem_strfree(tmpname);
	proc_create_data("datasets", 0444, zone->zone_proc,
	    &proc_zone_datasets_operations, zone);

	zone_kstat_create(zone);

	zone->zone_status = ZONE_IS_READY;
	mutex_exit(&zone->zone_lock);

	mutex_enter(&zonehash_lock);
	list_insert_tail(&zone_active, zone);
	mutex_exit(&zonehash_lock);
	return zone->zone_id;
}
EXPORT_SYMBOL(spl_zone_create);

zoneid_t
spl_zone_setzfs(zoneid_t zone_id, char *zfsbuf, size_t zfsbufsz,
    int *extended_error)
{
	zone_t *zone;

	zone = spl_zone_find_by_id(zone_id);

	if (!zone) {
		*extended_error = -EINVAL;
		return 0;
	}

	mutex_enter(&zone->zone_lock);
	spl_zone_free_datasets(zone);
	list_create(&zone->zone_datasets, sizeof (zone_dataset_t),
	    offsetof(zone_dataset_t, zd_linkage));

	__parse_zfs(zone, zfsbuf, zfsbufsz);
	mutex_exit(&zone->zone_lock);
	return zone_id;
}
EXPORT_SYMBOL(spl_zone_setzfs);

int
spl_zone_boot(zoneid_t zoneid, pid_t pid)
{
	zone_t *zone = spl_zone_find_by_id(zoneid);
	zone_status_t status = zone_status_get(zone);
	struct task_struct *task;

	int err = 0;

	if (!zone || (status < ZONE_IS_READY || status > ZONE_IS_DOWN))
		return -EINVAL;

	task = pid_task(find_vpid(pid), PIDTYPE_PID);

	if (!task)
		return -EINVAL;

	mutex_enter(&zone->zone_lock);
	zone->zone_userns = get_user_ns(task_cred_xxx(task, user_ns));

	zone->zone_status = ZONE_IS_RUNNING;
	mutex_exit(&zone->zone_lock);
	/*
	 * There might be cached userns for zone0, which we now marked as
	 * part of another Zone, easiest solution is to flush the whole zone0
	 * cache.
	 */
	spl_zuc_flush(&zone0);
	return err;
}
EXPORT_SYMBOL(spl_zone_boot);

int
spl_zone_shutdown(zoneid_t zoneid)
{
	return -ENOSYS;
}
EXPORT_SYMBOL(spl_zone_shutdown);

int
spl_zone_destroy(zoneid_t zoneid)
{
	zone_t *zone = spl_zone_find_by_id(zoneid);
	if (!zone)
		return -EINVAL;

	spl_zone_free(zone);
	return 0;
}
EXPORT_SYMBOL(spl_zone_destroy);

/*
 * Returns true if the named dataset is visible in the specified zone.
 * The 'write' parameter is set to 1 if the dataset is also writable.
 */
int
zone_dataset_visible_inzone(zone_t *zone, const char *dataset, int *write)
{
	zone_dataset_t *zd;
	size_t len;

	pr_debug("%s: zone_id %i dataset to check: %s\n", __func__,
	    zone->zone_id, dataset);

	if (dataset[0] == '\0')
		return (0);

	/*
	 * Walk the list once, looking for datasets which match exactly, or
	 * specify a dataset underneath an exported dataset.  If found, return
	 * true and note that it is writable.
	 */
	for (zd = list_head(&zone->zone_datasets); zd != NULL;
	    zd = list_next(&zone->zone_datasets, zd)) {

		len = strlen(zd->zd_dataset);
		if (strlen(dataset) >= len &&
		    bcmp(dataset, zd->zd_dataset, len) == 0 &&
		    (dataset[len] == '\0' || dataset[len] == '/' ||
		    dataset[len] == '@')) {
			if (write)
				*write = 1;
			if (write)
				pr_debug("%s: visible and %s writeable, zd_dataset %s dataset %s len %lu\n", __func__, (*write) ? "IS" : "ISNT", zd->zd_dataset, dataset, len);
			else
				pr_debug("%s: visible, zd_dataset %s dataset %s len %lu\n", __func__, zd->zd_dataset, dataset, len);
			return (1);
		}
	}

	/*
	 * Walk the list a second time, searching for datasets which are parents
	 * of exported datasets.  These should be visible, but read-only.
	 *
	 * Note that we also have to support forms such as 'pool/dataset/', with
	 * a trailing slash.
	 */
	for (zd = list_head(&zone->zone_datasets); zd != NULL;
	    zd = list_next(&zone->zone_datasets, zd)) {

		len = strlen(dataset);
		if (dataset[len - 1] == '/')
			len--;	/* Ignore trailing slash */
		if (len < strlen(zd->zd_dataset) &&
		    bcmp(dataset, zd->zd_dataset, len) == 0 &&
		    zd->zd_dataset[len] == '/') {
			if (write)
				*write = 0;
			return (1);
		}
	}

	/* TODO:
	 * should also check the root filesystem of given PID 1 of the "Zone"
	 * if it's a ZFS dataset, we should make it visible...
	 */
	return (0);
}
EXPORT_SYMBOL(zone_dataset_visible_inzone);

/*
 * Returns true if the named dataset is visible in the current zone.
 * The 'write' parameter is set to 1 if the dataset is also writable.
 */
int
zone_dataset_visible(const char *dataset, int *write)
{
	zone_t *zone = curzone;

	return (zone_dataset_visible_inzone(zone, dataset, write));
}
EXPORT_SYMBOL(zone_dataset_visible);

static zone_vfs_kstat_t zone_vfs_stats_template = {
	{ "zonename",			KSTAT_DATA_STRING },
	{ "nread",			KSTAT_DATA_UINT64 },
	{ "reads",			KSTAT_DATA_UINT64 },
	{ "rtime",			KSTAT_DATA_UINT64 },
	{ "rlentime",			KSTAT_DATA_UINT64 },
	{ "rcnt",			KSTAT_DATA_UINT64 },
	{ "nwritten",			KSTAT_DATA_UINT64 },
	{ "writes",			KSTAT_DATA_UINT64 },
	{ "wtime",			KSTAT_DATA_UINT64 },
	{ "wlentime",			KSTAT_DATA_UINT64 },
	{ "wcnt",			KSTAT_DATA_UINT64 },
	{ "10ms_ops",			KSTAT_DATA_UINT64 },
	{ "100ms_ops",			KSTAT_DATA_UINT64 },
	{ "1s_ops",			KSTAT_DATA_UINT64 },
	{ "10s_ops",			KSTAT_DATA_UINT64 },
	{ "delay_cnt",			KSTAT_DATA_UINT64 },
	{ "delay_time",			KSTAT_DATA_UINT64 },
};

static zone_zfs_kstat_t zone_zfs_stats_template = {
	{ "zonename",			KSTAT_DATA_STRING },
	{ "nread",			KSTAT_DATA_UINT64 },
	{ "reads",			KSTAT_DATA_UINT64 },
	{ "rtime",			KSTAT_DATA_UINT64 },
	{ "rlentime",			KSTAT_DATA_UINT64 },
	{ "nwritten",			KSTAT_DATA_UINT64 },
	{ "writes",			KSTAT_DATA_UINT64 },
	{ "waittime",			KSTAT_DATA_UINT64 },
};

static int
zone_zfs_kstat_update(kstat_t *ksp, int rw)
{
	zone_t *zone = ksp->ks_private;
	zone_zfs_kstat_t *zzp = ksp->ks_data;
	zone_persist_t *zp = &zone_pdata[zone->zone_id];

	if (rw == KSTAT_WRITE)
		return (EACCES);

	mutex_enter(&zp->zpers_zfs_lock);
	if (zp->zpers_zfsp == NULL) {
		zzp->zz_nread.value.ui64 = 0;
		zzp->zz_reads.value.ui64 = 0;
		zzp->zz_rtime.value.ui64 = 0;
		zzp->zz_rlentime.value.ui64 = 0;
		zzp->zz_nwritten.value.ui64 = 0;
		zzp->zz_writes.value.ui64 = 0;
		zzp->zz_waittime.value.ui64 = 0;
	} else {
		kstat_io_t *kiop = &zp->zpers_zfsp->zpers_zfs_rwstats;

		zzp->zz_nread.value.ui64 = kiop->nread;
		zzp->zz_reads.value.ui64 = kiop->reads;
		zzp->zz_rtime.value.ui64 = kiop->rtime;
		zzp->zz_rlentime.value.ui64 = kiop->rlentime;
		zzp->zz_nwritten.value.ui64 = kiop->nwritten;
		zzp->zz_writes.value.ui64 = kiop->writes;
		zzp->zz_waittime.value.ui64 =
		    zp->zpers_zfsp->zpers_zfs_rd_waittime;
	}
	mutex_exit(&zp->zpers_zfs_lock);

	return (0);
}

static int
zone_vfs_kstat_update(kstat_t *ksp, int rw)
{
	zone_t *zone = ksp->ks_private;
	zone_vfs_kstat_t *zvp = ksp->ks_data;
	kstat_io_t *kiop = &zone->zone_vfs_rwstats;

	if (rw == KSTAT_WRITE)
		return (EACCES);

	zvp->zv_nread.value.ui64 = kiop->nread;
	zvp->zv_reads.value.ui64 = kiop->reads;
	zvp->zv_rtime.value.ui64 = kiop->rtime;
	zvp->zv_rcnt.value.ui64 = kiop->rcnt;
	zvp->zv_rlentime.value.ui64 = kiop->rlentime;
	zvp->zv_nwritten.value.ui64 = kiop->nwritten;
	zvp->zv_writes.value.ui64 = kiop->writes;
	zvp->zv_wtime.value.ui64 = kiop->wtime;
	zvp->zv_wcnt.value.ui64 = kiop->wcnt;
	zvp->zv_wlentime.value.ui64 = kiop->wlentime;

	return (0);
}

static void
zone_vfs_kstat_create(zone_t *zone)
{
	kstat_t *ksp;
	zone_vfs_kstat_t *zvp;

	if (zone == NULL)
		return;

	mutex_init(&zone->zone_vfs_lock, NULL, MUTEX_DEFAULT, NULL);

	char *name = kmem_asprintf("zone_%i", zone->zone_id);
	ksp = kstat_create(name, 0, "vfs_stats", "misc", KSTAT_TYPE_NAMED,
	    sizeof (zone_vfs_kstat_t) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);

	if (ksp == NULL)
		goto out;

	zvp = ksp->ks_data = kmem_zalloc(sizeof (zone_vfs_kstat_t), KM_SLEEP);
	ksp->ks_data_size += strlen(zone->zone_name) + 1;
	ksp->ks_lock = &zone->zone_vfs_lock;
	zone->zone_vfs_stats = zvp;

	memcpy(ksp->ks_data, &zone_vfs_stats_template,
	    sizeof (zone_vfs_kstat_t));
	KSTAT_NAMED_STR_PTR(&zvp->zv_zonename) = zone->zone_name;
	if (zone->zone_name != NULL)
		KSTAT_NAMED_STR_BUFLEN(&zvp->zv_zonename) =
		    strlen(zone->zone_name) + 1;
	else
		KSTAT_NAMED_STR_BUFLEN(&zvp->zv_zonename) = 0;

	ksp->ks_update = zone_vfs_kstat_update;
	ksp->ks_private = zone;

	zone->zone_vfs_ksp = ksp;
	kstat_install(ksp);
out:
	kmem_strfree(name);
}

static void
zone_zfs_kstat_create(zone_t *zone)
{
	kstat_t *ksp;
	zone_zfs_kstat_t *zzp;

	mutex_init(&zone->zone_zfs_lock, NULL, MUTEX_DEFAULT, NULL);

	char *name = kmem_asprintf("zone_%i", zone->zone_id);
	ksp = kstat_create(name, 0, "zfs_stats", "misc", KSTAT_TYPE_NAMED,
	    sizeof (zone_zfs_kstat_t) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);

	if (ksp == NULL)
		goto out;

	zzp = ksp->ks_data = kmem_zalloc(sizeof (zone_zfs_kstat_t), KM_SLEEP);
	ksp->ks_data_size += strlen(zone->zone_name) + 1;
	ksp->ks_lock = &zone->zone_zfs_lock;
	zone->zone_zfs_stats = zzp;

	memcpy(ksp->ks_data, &zone_zfs_stats_template,
	    sizeof (zone_zfs_kstat_t));
	KSTAT_NAMED_STR_PTR(&zzp->zz_zonename) = zone->zone_name;
	if (zone->zone_name != NULL)
		KSTAT_NAMED_STR_BUFLEN(&zzp->zz_zonename) =
		    strlen(zone->zone_name) + 1;
	else
		KSTAT_NAMED_STR_BUFLEN(&zzp->zz_zonename) = 0;

	ksp->ks_update = zone_zfs_kstat_update;
	ksp->ks_private = zone;

	zone->zone_zfs_ksp = ksp;
	kstat_install(ksp);
out:
	kmem_strfree(name);
}

static void
zone_kstat_create(zone_t *zone)
{
	zone_vfs_kstat_create(zone);
	zone_zfs_kstat_create(zone);
}

static void
zone_kstat_delete_common(kstat_t **pkstat, size_t datasz)
{
	void *data;

	if (*pkstat != NULL) {
		data = (*pkstat)->ks_data;
		mutex_destroy((*pkstat)->ks_lock);
		kstat_delete(*pkstat);
		kmem_free(data, datasz);
		*pkstat = NULL;
	}
}

static void
zone_kstat_delete(zone_t *zone)
{
	zone_kstat_delete_common(&zone->zone_vfs_ksp,
	    sizeof (zone_vfs_kstat_t));
	zone_kstat_delete_common(&zone->zone_zfs_ksp,
	    sizeof (zone_zfs_kstat_t));
}

/*
 * Uncache all userns cached for a given zone
 */
static void
spl_zuc_flush(zone_t *zone)
{
	zone_userns_cache_list_t *t, *next;

	mutex_enter(&zonehash_lock);
	for (t = list_head(&zone->zone_userns_cache_list); t != NULL;
	    t = next) {
		next = list_next(&zone->zone_userns_cache_list, t);
		list_remove(&zone->zone_userns_cache_list, t);
		mutex_enter(&zone_userns_cache_list_lock);
		list_remove(&zone_userns_cache_list, t);
		mutex_exit(&zone_userns_cache_list_lock);
		zone->zone_numuserns--;
		put_user_ns(t->zuc_userns);
		btree_remove64(&zone_by_userns, (uintptr_t)t->zuc_userns);
		kmem_free(t, sizeof (*t));
	}
	mutex_exit(&zonehash_lock);
	pr_debug("%s: flushed zone userns cache of zone %i\n", __func__, zone->zone_id);
}

static void
spl_zuc_evict_once(void)
{
	list_t clean_list;
	zone_userns_cache_list_t *t, *next;

	hrtime_t now = gethrtime();

	/*
	 * Currently, we cache descendant user namespaces to the
	 * registered one the Zone was originally "booted" with;
	 * When inserting the userns in cache, we increase refcount on
	 * it to make sure it won't dissapear meanwhile.
	 * Doing this, we should periodically check, whether we're the
	 * last to hold a reference to a cached userns, if so, uncache
	 * it and lower the refcount to allow freeing of the userns.
	 */
	list_create(&clean_list,
	    sizeof (zone_userns_cache_list_t),
	    offsetof(zone_userns_cache_list_t, zuc_node_global));

	mutex_enter(&zone_userns_cache_list_lock);
	for (t = list_head(&zone_userns_cache_list);
	    (global_zone != NULL) && (t != NULL);
	    t = next) {
		next = list_next(&zone_userns_cache_list, t);
		int refcount = atomic_read(&t->zuc_userns->count);
		if ((now - t->zuc_last_access) > SEC2NSEC(15)) {
			pr_debug("%s: evict userns %px refcnt %d\n",
			    __func__, (void *)t->zuc_userns, refcount);
			list_remove(&zone_userns_cache_list, t);
			list_insert_tail(&clean_list, t);

		}
	}
	mutex_exit(&zone_userns_cache_list_lock);

	mutex_enter(&zonehash_lock);
	for (t = list_head(&clean_list);
	    (global_zone != NULL) && (t != NULL);
	    t = next) {
		next = list_next(&clean_list, t);
		zone_t *zone = t->zuc_zone;

		btree_remove64(&zone_by_userns, (uintptr_t)t->zuc_userns);
		list_remove(&zone->zone_userns_cache_list, t);
		zone->zone_numuserns--;

		put_user_ns(t->zuc_userns);
		kmem_free(t, sizeof (*t));
	}
	mutex_exit(&zonehash_lock);
	list_destroy(&clean_list);
}

kthread_t *spl_zuc_evict_thread = NULL;

static void
spl_zuc_evict(void *unused)
{
	while (1) {
		msleep(1000);
		if (global_zone == NULL)
			thread_exit();
		spl_zuc_evict_once();
	}
}

/* zonehash_lock assumed held */
static zone_userns_cache_list_t *
spl_zuc_lookup_by_userns_impl(struct user_namespace *userns)
{
	zone_userns_cache_list_t *zucp = NULL;

	ASSERT(MUTEX_HELD(&zonehash_lock));

	if (unlikely(!userns))
		return NULL;
	zucp = btree_lookup64(&zone_by_userns, (uintptr_t)userns);
	if (zucp)
		zucp->zuc_last_access = gethrtime();
	return zucp;
}

/* zonehash_lock assumed held */
static void
spl_zuc_insert_impl(zone_t *z, struct user_namespace *userns)
{
	zone_t *zone = (z) ? z : &zone0;
	zone_userns_cache_list_t *zucp;

	pr_debug("%s: zone: %i userns: %px\n", __func__, zone->zone_id, (void *)userns);
	ASSERT(userns != NULL);
	ASSERT(MUTEX_HELD(&zonehash_lock));
	ASSERT(spl_zuc_lookup_by_userns_impl(userns) == NULL);

	zucp = kmem_zalloc(sizeof(zone_userns_cache_list_t), KM_SLEEP);
	zucp->zuc_userns = get_user_ns(userns);
	zucp->zuc_zone = zone;
	zucp->zuc_last_access = gethrtime();
	list_insert_head(&zone->zone_userns_cache_list, zucp);
	mutex_enter(&zone_userns_cache_list_lock);
	list_insert_head(&zone_userns_cache_list, zucp);
	mutex_exit(&zone_userns_cache_list_lock);
	zone->zone_numuserns++;
	btree_insert64(&zone_by_userns,
	    (uintptr_t)userns, zucp, GFP_ATOMIC);
}

/* zonehash_lock assumed held */
static zone_t *
spl_getzone_by_userns_impl(struct user_namespace *userns, bool cache_result)
{
	zoneid_t zone_id;
	zone_t *zone = NULL;
	zone_t *zter = NULL;

	ASSERT(userns != NULL);
	ASSERT(MUTEX_HELD(&zonehash_lock));

	if (!userns)
		return NULL;

	if (userns == &init_user_ns)
		return NULL;

	/* Cached lookup first */
	zone_userns_cache_list_t *zucp = spl_zuc_lookup_by_userns_impl(userns);
	if (zucp) {
		ASSERT(!zucp->zuc_invalid);
		return zucp->zuc_zone;
	}

	/*
	 * Well, looks like we have to search the zone space for this user_ns
	 */
	idr_for_each_entry(&zoneid_space, zter, zone_id) {
		if (zter &&
		    zter->zone_userns == userns) {
//			if (zone_id == GLOBAL_ZONEID)
//				continue;
			/* Success: this zone belongs to this userns */
			if (cache_result)
				spl_zuc_insert_impl(zter, userns);
			return zter;
		}
	}

	/* 
	 * This userns wasn't found to belong to a non-global zone,
	 * check if parent isn't init_user_ns, if not, then we recurse one
	 * level deeper into the parent user_ns.
	 */
	if (userns->parent == &init_user_ns)
		zone = NULL;
	else
		zone = spl_getzone_by_userns_impl(userns->parent, false);

	if (cache_result) {
		spl_zuc_insert_impl(zone, userns);
	}

	return zone;
}

/*
 * Names corresponding to zone_status_t values in sys/zone.h
 */
char *zone_status_names[] = {
	"uninitialized",	/* ZONE_IS_UNINITIALIZED */
	"initialized",		/* ZONE_IS_INITIALIZED */
	"ready",		/* ZONE_IS_READY */
	"booting",		/* ZONE_IS_BOOTING */
	"running",		/* ZONE_IS_RUNNING */
	"shutting_down",	/* ZONE_IS_SHUTTING_DOWN */
	"empty",		/* ZONE_IS_EMPTY */
	"down",			/* ZONE_IS_DOWN */
	"dying",		/* ZONE_IS_DYING */
	"dead",			/* ZONE_IS_DEAD */
	"free"			/* ZONE_IS_FREE */
};

static void
zone_seq_show_headers(struct seq_file *f)
{
	seq_printf(f,
	    "  ZONE NAME                          |   ZONE ID  |   STATUS    "
	    "    |   HOST ID  |   # DSETS  |  ORIG USERNS     |   # USERNS\n");
	seq_printf(f,
	    "------------------------------------   ----------   ------------"
	    "---   ----------   ----------   ----------------   ----------\n");
}

static int
zone_seq_show(struct seq_file *f, void *p)
{
	zone_t *zone = p;

	mutex_enter(&zone->zone_lock);
	seq_printf(f, "%-36s", zone->zone_name);
	seq_printf(f, "   %10lu    %-14s   %10lu   %10lu   %px   %10lu\n",
	    (long unsigned)zone->zone_id,
	    zone_status_names[zone->zone_status],
	    (long unsigned)zone->zone_hostid,
	    (long unsigned)zone->zone_numdatasets,
	    (void *)zone->zone_userns,
	    (long unsigned)zone->zone_numuserns);
	mutex_exit(&zone->zone_lock);
	return (0);
}

static void *
zone_seq_start(struct seq_file *f, loff_t *pos)
{
	struct list_head *p;
	loff_t n = *pos;

	if (!n)
		zone_seq_show_headers(f);

	mutex_enter(&zonehash_lock);
	p = zone_active.list_head.next;
	while (n--) {
		p = p->next;
		if (p == &zone_active.list_head)
			return (NULL);
	}

	return (list_entry(p, zone_t, zone_linkage));
}

static void *
zone_seq_next(struct seq_file *f, void *p, loff_t *pos)
{
	zone_t *zone = p;

	++*pos;
	return ((zone->zone_linkage.next == &zone_active.list_head) ?
	    NULL : list_entry(zone->zone_linkage.next, zone_t, zone_linkage));
}

static void
zone_seq_stop(struct seq_file *f, void *v)
{
	mutex_exit(&zonehash_lock);
}

static struct seq_operations zone_seq_ops = {
	.show  = zone_seq_show,
	.start = zone_seq_start,
	.next  = zone_seq_next,
	.stop  = zone_seq_stop,
};

static int
proc_zone_open(struct inode *inode, struct file *filp)
{
	return (seq_open(filp, &zone_seq_ops));
}

static int
zone_datasets_seq_show(struct seq_file *f, void *p)
{
	zone_dataset_t *zd = p;

	seq_printf(f, "%-36s\n", zd->zd_dataset);
	return (0);
}

static void *
zone_datasets_seq_start(struct seq_file *f, loff_t *pos)
{
	zone_t *zone = f->private;
	struct list_head *p;
	loff_t n = *pos;

	mutex_enter(&zone->zone_lock);
	p = zone->zone_datasets.list_head.next;
	while (n--) {
		p = p->next;
		if (p == &zone->zone_datasets.list_head)
			return (NULL);
	}

	return (list_entry(p, zone_dataset_t, zd_linkage));
}

static void *
zone_datasets_seq_next(struct seq_file *f, void *p, loff_t *pos)
{
	zone_t *zone = f->private;
	zone_dataset_t *zd = p;

	++*pos;
	return ((zd->zd_linkage.next == &zone->zone_datasets.list_head) ?
	    NULL : list_entry(zd->zd_linkage.next, zone_dataset_t, zd_linkage));
}

static void
zone_datasets_seq_stop(struct seq_file *f, void *v)
{
	zone_t *zone = f->private;
	mutex_exit(&zone->zone_lock);
}

static struct seq_operations zone_datasets_seq_ops = {
	.show  = zone_datasets_seq_show,
	.start = zone_datasets_seq_start,
	.next  = zone_datasets_seq_next,
	.stop  = zone_datasets_seq_stop,
};

static int
proc_zone_datasets_open(struct inode *inode, struct file *filp)
{
	int ret = seq_open(filp, &zone_datasets_seq_ops);
	if (ret == 0) {
		struct seq_file *m = filp->private_data;
		m->private = PDE_DATA(inode);
	}
	return ret;
}

static const kstat_proc_op_t proc_zone_datasets_operations = {
#ifdef HAVE_PROC_OPS_STRUCT
	.proc_open	= proc_zone_datasets_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release,
#else
	.open	 	= proc_zone_datasets_open,
	.read	 	= seq_read,
	.llseek	 	= seq_lseek,
	.release 	= seq_release,
#endif
};

typedef enum {
	CMD_CREATE,
	CMD_BOOT,
	CMD_SETZFS,
	CMD_SHUTDOWN,
	CMD_DESTROY,
	OPT_ID,
	OPT_INIT,
	OPT_NAME,
	OPT_DATASETS,
	OPT_LAST,
} zone_cmd_enum_t;

static const match_table_t zone_cmd_tokens = {
	{ CMD_CREATE,	"create" },
	{ CMD_BOOT,	"boot" },
	{ CMD_SETZFS,	"set-zfs" },
	{ CMD_SHUTDOWN,	"shutdown" },
	{ CMD_DESTROY,	"destroy" },
	{ OPT_ID,	"#%u" },
	{ OPT_ID,	"id=%u" },
	{ OPT_INIT,	"init=%u" },
	{ OPT_NAME,	"name=%s" },
	{ OPT_DATASETS,	"datasets=%s" },
	{ OPT_LAST,	NULL }
};

/*
 * Parses a comma-separated list of ZFS datasets into a per-zone dictionary.
 */
static int
__parse_zfs(zone_t *zone, char *kbuf, size_t buflen)
{
	char *dataset, *next;
	zone_dataset_t *zd;
	size_t len;

	dataset = next = kbuf;
	for (;;) {
		zd = kmem_zalloc(sizeof (zone_dataset_t), KM_SLEEP);

		next = strchr(dataset, ',');

		if (next == NULL)
			len = strlen(dataset);
		else
			len = next - dataset;

		zd->zd_dataset = kmem_zalloc(len + 1, KM_SLEEP);
		bcopy(dataset, zd->zd_dataset, len);
		zd->zd_dataset[len] = '\0';
		zone->zone_numdatasets++;

		list_insert_head(&zone->zone_datasets, zd);

		if (next == NULL)
			break;

		dataset = next + 1;
	}

	return (0);
}

ssize_t
proc_zone_write(struct file *file, const char __user *buf, size_t cmdlen,
    loff_t *offset)
{
	char *cmdbuf, *cmdptr;
	char *p = NULL, *zone_name = NULL, *zfsbuf = NULL;
	size_t zlen = 0, nlen = 0;
	zoneid_t zone_id = 0;
	pid_t reg_pid = 0;
	zone_cmd_enum_t exec = OPT_LAST;
	int err;

	if (cmdlen < 3)
		return 0;

	cmdptr = cmdbuf = kmem_zalloc(cmdlen, KM_SLEEP);
	if (copy_from_user(cmdbuf, buf, cmdlen-1)) {
		kmem_free(cmdbuf, cmdlen);
		return -EFAULT;
	}

	if (cmdbuf[cmdlen-1] == '\n')
		cmdbuf[cmdlen-1] = '\0';
	else
		cmdbuf[cmdlen] = '\0';

	while ((p = strsep(&cmdptr, " ")) != NULL) {
		substring_t args[MAX_OPT_ARGS];
		int token;

		if (!*p)
			continue;

		token = match_token(p, zone_cmd_tokens, args);

		switch (token) {
		case CMD_CREATE:
			exec = CMD_CREATE;
			break;
		case CMD_BOOT:
			exec = CMD_BOOT;
			break;
		case CMD_SETZFS:
			exec = CMD_SETZFS;
			break;
		case CMD_SHUTDOWN:
			exec = CMD_SHUTDOWN;
			break;
		case CMD_DESTROY:
			exec = CMD_DESTROY;
			break;
		case OPT_DATASETS:
			if (zfsbuf)
				break;
			zfsbuf = kmem_zalloc(PAGE_SIZE, KM_SLEEP);
			zlen = match_strlcpy(zfsbuf, &args[0],
			    ZFS_MAX_DATASET_NAME_LEN);
			if (zfsbuf[zlen-1] == '\n')
				zfsbuf[zlen-1] = '\0';
			break;
		case OPT_NAME:
			if (zone_name)
				break;
			zone_name = kmem_zalloc(ZONE_NAMELEN, KM_SLEEP);
			nlen = match_strlcpy(zone_name, &args[0], ZONE_NAMELEN);
			if (zone_name[nlen-1] == '\n')
				zone_name[nlen-1] = '\0';
			break;
		case OPT_ID:
			match_int(&args[0], &zone_id);
			break;
		case OPT_INIT:
			match_int(&args[0], &reg_pid);
			break;
		}
	}

	/* Validate inputs and execute command */
	switch (exec) {
	case CMD_CREATE:
		if (zone_name && zfsbuf && !zone_id)
			spl_zone_create(zone_name, zfsbuf, zlen, &err);
		else
			err = -EINVAL;
		break;
	case CMD_SETZFS:
		if (zone_id && zfsbuf)
			spl_zone_setzfs(zone_id, zfsbuf, zlen, &err);
		else
			err = -EINVAL;
		break;
	case CMD_BOOT:
		if (zone_id && reg_pid)
			err = spl_zone_boot(zone_id, reg_pid);
		else
			err = -EINVAL;
		break;
	case CMD_SHUTDOWN:
		if (zone_id)
			err = spl_zone_shutdown(zone_id);
		else
			err = -EINVAL;
		break;
	case CMD_DESTROY:
		if (zone_id)
			err = spl_zone_destroy(zone_id);
		else
			err = -EINVAL;
		break;
	default:
		err = -EINVAL;
	}

	kmem_free(cmdbuf, cmdlen);
	if (zone_name)
		kmem_free(zone_name, ZONE_NAMELEN);
	if (zfsbuf)
		kmem_strfree(zfsbuf);

	if (err != 0)
		return err;
	return cmdlen;
}

static const kstat_proc_op_t proc_zone_operations = {
#ifdef HAVE_PROC_OPS_STRUCT
	.proc_open	= proc_zone_open,
	.proc_write	= proc_zone_write,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release,
#else
	.open		= proc_zone_open,
	.write		= proc_zone_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
#endif
};

static ssize_t
proc_curzone_read(struct file *filp, char *buf, size_t count, loff_t *pos) 
{
	zone_t *zone = curzone;
	char msg[256];
	int len = 0;

	if(*pos > 0 || count < 256)
		return 0;

	len = scnprintf(msg, 256, "%u\n", zone->zone_id);
	copy_to_user(buf, msg, len);
	*pos = len;
	return len;
}

static const kstat_proc_op_t proc_curzone_operations = {
#ifdef HAVE_PROC_OPS_STRUCT
	.proc_read	= proc_curzone_read,
#else
	.read		= proc_curzone_read,
#endif
};

int
spl_zone_init(void)
{
	btree_init64(&zone_by_userns);
	mutex_init(&zonehash_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&zone_active, sizeof (zone_t),
	    offsetof(zone_t, zone_linkage));
	list_create(&zone_userns_cache_list, sizeof (zone_userns_cache_list_t),
	    offsetof(zone_userns_cache_list_t, zuc_node_global));
	mutex_init(&zone_userns_cache_list_lock, NULL, MUTEX_DEFAULT, NULL);

	mutex_init(&zone0.zone_lock, NULL, MUTEX_DEFAULT, NULL);
	zone0.zone_name = kmem_zalloc(ZONE_NAMELEN, KM_SLEEP);
	memcpy(zone0.zone_name, GLOBAL_ZONENAME, strlen(GLOBAL_ZONENAME));
	zone0.zone_hostid = HW_INVALID_HOSTID;
	zone0.zone_id = GLOBAL_ZONEID;
	zone0.zone_status = ZONE_IS_RUNNING;
	zone0.zone_userns = &init_user_ns;
	zone0.zone_numuserns = 0;
	zone0.zone_numdatasets = 0;

	list_create(&zone0.zone_userns_cache_list, sizeof (zone_userns_cache_list_t),
	    offsetof(zone_userns_cache_list_t, zuc_node));
	list_create(&zone0.zone_datasets, sizeof (zone_dataset_t),
	    offsetof(zone_dataset_t, zd_linkage));

	zone_pdata[GLOBAL_ZONEID].zpers_zfsp = &zone0_zp_zfs;
	zone_pdata[GLOBAL_ZONEID].zpers_zfsp->zpers_zfs_io_pri = 1;
	mutex_init(&zone_pdata[GLOBAL_ZONEID].zpers_zfs_lock, NULL, MUTEX_DEFAULT, NULL);

	zone_kstat_create(&zone0);

	mutex_enter(&zonehash_lock);
	list_insert_head(&zone_active, &zone0);
	ASSERT(idr_alloc(&zoneid_space, &zone0,
	    GLOBAL_ZONEID, GLOBAL_ZONEID, GFP_KERNEL) == GLOBAL_ZONEID);
	mutex_exit(&zonehash_lock);

	global_zone = &zone0;

	proc_create_data("curzone", 0444, proc_spl,
	    &proc_curzone_operations, NULL);

	proc_spl_zone = proc_create_data("zone", 0444, proc_spl,
	    &proc_zone_operations, NULL);

	spl_zuc_evict_thread = thread_create(NULL, 0, spl_zuc_evict, NULL, 0, 
	    curproc, TS_RUN, minclsyspri);
//	spl_zuc_evict(NULL);

	return 0;
}

void
spl_zone_fini(void)
{
	zone_t *zone;
	zoneid_t zone_id;

	global_zone = NULL;
	kthread_stop(spl_zuc_evict_thread);

	idr_remove(&zoneid_space, GLOBAL_ZONEID);
	idr_for_each_entry(&zoneid_space, zone, zone_id) {
		spl_zone_destroy(zone_id);
	}

	spl_zuc_flush(&zone0);
	list_destroy(&zone0.zone_userns_cache_list);
	zone_kstat_delete(&zone0);
	kmem_free(zone0.zone_name, ZONE_NAMELEN);
	mutex_destroy(&zone0.zone_lock);
	mutex_destroy(&zone_pdata[GLOBAL_ZONEID].zpers_zfs_lock);

	remove_proc_entry("curzone", proc_spl);
	remove_proc_entry("zone", proc_spl);
	btree_destroy64(&zone_by_userns);

	list_destroy(&zone_userns_cache_list);
	mutex_destroy(&zone_userns_cache_list_lock);
	list_destroy(&zone_active);
	mutex_destroy(&zonehash_lock);
}
