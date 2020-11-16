/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
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

#ifndef _SPL_ZONE_H
#define	_SPL_ZONE_H

#include <sys/types.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/taskq.h>
#include <sys/kstat.h>
#include <sys/byteorder.h>

#include <sys/mutex.h>
#include <sys/cred.h>
#include <sys/condvar.h>
#include <sys/nvpair.h>
#include <sys/list.h>

/*
 * zone id restrictions and special ids.
 * We have inherited MAX_ZONES limit from Illumos, though there's nothing
 * that would actually impose this limit on Linux.
 */

#define	MAX_ZONES	8192
#define	MIN_ZONEID	0	/* minimum zone ID on system */
#define	MIN_USERZONEID	1	/* lowest user-creatable zone ID */
#define	MAX_ZONEID	(MAX_ZONES - 1)
#define	GLOBAL_ZONEID	0
#define	GLOBAL_ZONENAME	"global"
#define ZONE_NAMELEN	36

/*
 * Special zoneid_t token to refer to all zones.
 */
#define	ALL_ZONES	(-1)

typedef enum {
	ZONE_IS_UNINITIALIZED = 0,
	ZONE_IS_INITIALIZED,
	ZONE_IS_READY,
	ZONE_IS_BOOTING,
	ZONE_IS_RUNNING,
	ZONE_IS_SHUTTING_DOWN,
	ZONE_IS_EMPTY,
	ZONE_IS_DOWN,
	ZONE_IS_DYING,
	ZONE_IS_DEAD,
	ZONE_IS_FREE		/* transient state for zone sysevent */
} zone_status_t;
#define	ZONE_MIN_STATE		ZONE_IS_UNINITIALIZED
#define	ZONE_MAX_STATE		ZONE_IS_DEAD

/*
 * Structure to record list of ZFS datasets exported to a zone.
 */
typedef struct zone_dataset {
	char		*zd_dataset;
	list_node_t	zd_linkage;
} zone_dataset_t;

typedef struct {
	hrtime_t	cycle_start;
	uint_t		cycle_cnt;
	hrtime_t	zone_avg_cnt;
} sys_zio_cntr_t;

typedef struct {
	kstat_named_t	zv_zonename;
	kstat_named_t	zv_nread;
	kstat_named_t	zv_reads;
	kstat_named_t	zv_rtime;
	kstat_named_t	zv_rlentime;
	kstat_named_t	zv_rcnt;
	kstat_named_t	zv_nwritten;
	kstat_named_t	zv_writes;
	kstat_named_t	zv_wtime;
	kstat_named_t	zv_wlentime;
	kstat_named_t	zv_wcnt;
	kstat_named_t	zv_10ms_ops;
	kstat_named_t	zv_100ms_ops;
	kstat_named_t	zv_1s_ops;
	kstat_named_t	zv_10s_ops;
	kstat_named_t	zv_delay_cnt;
	kstat_named_t	zv_delay_time;
} zone_vfs_kstat_t;

typedef struct {
	kstat_named_t	zz_zonename;
	kstat_named_t	zz_nread;
	kstat_named_t	zz_reads;
	kstat_named_t	zz_rtime;
	kstat_named_t	zz_rlentime;
	kstat_named_t	zz_nwritten;
	kstat_named_t	zz_writes;
	kstat_named_t	zz_waittime;
} zone_zfs_kstat_t;

/*
 * Data and counters used for ZFS fair-share disk IO.
 */
typedef struct zone_zfs_io {
	uint16_t	zpers_zfs_io_pri;	/* ZFS IO priority - 16k max */
	uint_t		zpers_zfs_queued[2];	/* sync I/O enqueued count */
	sys_zio_cntr_t	zpers_rd_ops;		/* Counters for ZFS reads, */
	sys_zio_cntr_t	zpers_wr_ops;		/* writes, and */
	sys_zio_cntr_t	zpers_lwr_ops;		/* logical writes. */
	kstat_io_t	zpers_zfs_rwstats;
	uint64_t	zpers_io_util;		/* IO utilization metric */
	uint64_t	zpers_zfs_rd_waittime;
	uint8_t		zpers_io_delay;		/* IO delay on logical r/w */
	uint8_t		zpers_zfs_weight;	/* used to prevent starvation */
	uint8_t		zpers_io_util_above_avg; /* IO util percent > avg. */
} zone_zfs_io_t;

/*
 * "Persistent" zone data which can be accessed idependently of the zone_t.
 */
typedef struct zone_persist {
	kmutex_t	zpers_zfs_lock;	/* Protects zpers_zfsp references */
	zone_zfs_io_t	*zpers_zfsp;	/* ZFS fair-share IO data */
	uint8_t		zpers_over;	/* currently over cap */
} zone_persist_t;

/*
 * struct spl_zone loosely emulates Illumos struct zone
 */
typedef struct spl_zone {
	kmutex_t	zone_lock;	/* protects: zone_name, zone_datasets,
					 * linux-specific variables */
	char		*zone_name;	/* zone's configuration name */
	uint32_t	zone_hostid;	/* zone's hostid, HW_INVALID_HOSTID */
					/* if not emulated */
	/*
	 * zone_linkage is the zone's linkage into the active list. The field
	 * is protected by zonehash_lock.
	 */
	list_node_t     zone_linkage;
	zoneid_t	zone_id;	/* ID of zone */

//?	struct vnode	*zone_rootvp;	/* zone's root vnode */
//?	char		*zone_rootpath;	/* Path to zone's root + '/' */
	zone_status_t	zone_status;	/* protected by zone_status_lock */

	/*
	 * List of ZFS datasets exported to this zone.
	 */
	list_t		zone_datasets;		/* list of datasets */
	uint32_t	zone_numdatasets;	/* number of datasets */

	/*
	 * kstats and counters for VFS ops and bytes.
	 */
	kmutex_t	zone_vfs_lock;		/* protects VFS statistics */
	kstat_t		*zone_vfs_ksp;
	kstat_io_t	zone_vfs_rwstats;
	zone_vfs_kstat_t *zone_vfs_stats;

	/*
	 * kstats for ZFS I/O ops and bytes.
	 */
	kmutex_t	zone_zfs_lock;		/* protects ZFS statistics */
	kstat_t		*zone_zfs_ksp;
	zone_zfs_kstat_t *zone_zfs_stats;

	/*
	 * Linux specific
	 */
	
	struct proc_dir_entry *zone_proc;
	uint64_t	zone_numuserns;	/* number of cached userns */
	struct user_namespace *zone_userns;
	list_t		zone_userns_cache_list;
	struct task_struct *init_task;
} zone_t;

typedef struct zone_userns_cache_list {
	list_node_t	zuc_node;	/* zone->zone_userns_cache_list */
	list_node_t	zuc_node_global; /* all zone_userns_cache_list_t's */
	zone_t		*zuc_zone;
	struct user_namespace *zuc_userns;
	bool		zuc_invalid;	/* Uncached because we were last to
					 * reference this userns? */
	hrtime_t	zuc_last_access;
} zone_userns_cache_list_t;

extern zone_persist_t zone_pdata[MAX_ZONES];
//extern long zone(int, void *, void *, void *, void *);
extern int spl_zone_init(void);
extern void spl_zone_fini(void);
extern zone_t *spl_zone_find_by_id(zoneid_t);
extern zoneid_t getzoneid(void);
extern zoneid_t crgetzoneid(cred_t *);
extern zoneid_t getzoneid_task(struct task_struct *);

extern int zone_walk(int (*)(zone_t *, void *), void *);

extern zoneid_t spl_zone_create(char *zone_name, char *zfsbuf, size_t zfsbufsz,
    int *extended_error);
extern zoneid_t spl_zone_setzfs(zoneid_t zone_id, char *zfsbuf, size_t zfsbufsz,
    int *extended_error);
extern int spl_zone_boot(zoneid_t zoneid, pid_t pid);
extern int spl_zone_destroy(zoneid_t zoneid);
extern int spl_zone_shutdown(zoneid_t zoneid);
extern void spl_zone_free(zone_t *);

extern zone_t *currentzone(void);
static inline zone_t *
_curzone(void)
{
	return currentzone();
}
#define curzone (_curzone()) /* current zone pointer */

/*
 * Get the status  of the zone (at the time it was called).  The state may
 * have progressed by the time it is returned.
 */
extern zone_status_t zone_status_get(zone_t *);

/*
 * Returns true if the named pool/dataset is visible in the current zone.
 */
extern int zone_dataset_visible(const char *, int *);
extern int zone_dataset_visible_inzone(zone_t *, const char *, int *);

#define	INGLOBALZONE(z)			(GLOBAL_ZONEID == getzoneid_task(z))

#endif /* SPL_ZONE_H */
