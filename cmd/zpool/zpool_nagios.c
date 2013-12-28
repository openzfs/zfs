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
 * Copyright (c) 2013 by DeHackEd. All rights reserved.
 */

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <libintl.h>
#include <libuutil.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <priv.h>
#include <pwd.h>
#include <zone.h>
#include <zfs_prop.h>
#include <sys/fs/zfs.h>
#include <sys/stat.h>
#include <sys/fm/util.h>
#include <sys/fm/protocol.h>

#include <libzfs.h>

#include "zfs_comutil.h"
#include "zfeature_common.h"
#include "zpool_util.h"

#include "statcommon.h"

static void print_alerts(void);
static void alert(const char*, int);

/* From zpool_main.c (lazy code recycling) */
typedef struct status_cbdata {
	int		cb_count;
	boolean_t	cb_allpools;
	boolean_t	cb_verbose;
	boolean_t	cb_explain;
	boolean_t	cb_first;
	boolean_t	cb_dedup_stats;
} status_cbdata_t;

/* By recycling the "print pool status" code it's necessary to know
 * what type of device I'm looking at, or came from with my parents.
 */
typedef enum printstate {
	/* A disk, or other endpoint device */
	PSTATE_DISK,

	/* The pool as a whole (top-level) */
	PSTATE_POOL,

	/* A (redundant) vdev, such as raid-z or mirror */
	PSTATE_VDEV,

	/* A virtual device indicating disk replacement, like "replacing-1" */
	PSTATE_REPLACING,

	/* A log device */
	PSTATE_LOG,

	/* An L2ARC/cache (SSD) */
	PSTATE_CACHE,

	/* Spare disk */
	PSTATE_SPARE,

	PSTATE_UNKNOWN

} printstate;

static printstate currentstate = PSTATE_UNKNOWN;

/* Allows a disk to propagate its state upstream
 * allowing a DEGRADED state to be tracked.
 * Both are present so simultaneous offline and faulted
 * states are recognized correctly.
 */
static char child_faulted = 0;
static char child_offline = 0;

/* Command-line options affecting behaviour */
static char p_ok = 0;
static char p_warning = 0;
static char p_critical = 0;



/* All char types are boolean and have exactly values 0 or 1. This allows
 * checking if multiple fields are set by saying (a+b+c+d+e) > 1;
 * Other data types are relevant in full range's detail.
 */
struct alerts {
	char poolname[128];

	/* Pool scans (scrub or resilver) in progress */
	char rebuilding;
	char scrubbing;
	/* From 0.000 to 1.000 (can exceed 100% due to scrub mechanics */
	double percent;
	/* In seconds */
	int eta;


	/* IO errors or problems with a drive */
	char read_errors;
	char write_errors;
	char checksum_errors;
	char faulted;
	/* Same as faulted, but said fault disk is already part of a rebuild
	 * so it's more tolerable as an error condition.
	 */
	char faulted_rebuilding;
	char offline;
	char missing;
	char other_problems;

	/* Problems with a vdev (vdev redundancy has failed to protect data) */
	char vdev_degraded;
	char vdev_faulted;
	char vdev_checksum_errors;

	/* Problems with an L2ARC/CACHE */
	char cache_checksum_errors;
	char cache_io_errors;

	/* Problems with a pool (!!) */
	char permanent_data_errors;
	char pool_failed;

	/* Other types of informative messages */
	char admin_required;
	/* errata is actually an integer, 0 meaning none. */
	zpool_errata_t errata;


	/* "Low priority" messages */
	char upgrade_available;
};



/* Global variables are bad programming practice. Right? */
struct alerts alerts;
static int alertlevel = 0;
static int numalerts;

static void alert(const char *message, int x) {
	if (x==2)
		alertlevel = 2;
	else if (x == 1 && alertlevel != 2)
		alertlevel = 1;
	else if (x == 3 && alertlevel == 0)
		alertlevel = 3;
	/* else, don't do anything. Keep current alert level */


	printf("%s; ", message);
	numalerts++;
}


static void print_alerts() {
	char buffer[128];
	char use_clear = 0;
	printf("*%s*: ", alerts.poolname);

	int errors = alerts.read_errors+alerts.write_errors+alerts.checksum_errors;
	if ((errors) > 1) {
		alert("Serious I/O errors on disks", 2);
		use_clear = 1;
	} else if (errors > 0) {
		if (alerts.read_errors)
			alert("Read errors on disks", (p_critical) ? 2 : 1);
		else if (alerts.write_errors)
			alert("Write errors on disks", (p_critical) ? 2 : 1);
		else
			alert("Checksum errors on disks",(p_ok) ? 0 : 1);
		use_clear = 1;
	}

	if (alerts.offline)
		alert("Some disks(s) offlined by admin", (p_ok) ? 0 : 1);

	if (alerts.missing)
		alert("Some disks(s) missing/removed", p_warning ? 1 : 2);

	if (alerts.faulted)
		alert("Faulted disk(s)", 2);
	else if (alerts.faulted_rebuilding)
		alert("Faulted disk(s) being rebuilt", (p_ok) ? 0 : 1);

	if (alerts.vdev_degraded)
		alert("Vdev is degraded", 2);
	if (alerts.vdev_faulted)
		alert("Vdev failed", 2);
	if (alerts.vdev_checksum_errors) {
		/* Vdev checksum errors are just warnings because copies=2 can protect
		 * us from data loss. But if that fails or copies=1 then it will also
		 * generate other alerts.
		 */
		alert("Vdev internal checksum errors", 1);
	}
	if (alerts.permanent_data_errors)
		alert("Unresolvable data corruption/loss", 2);

	if (alerts.pool_failed)
		alert("Total pool failure", 2);



	if (alerts.other_problems)
		alert("Other/unknown issues requiring attention", (p_warning) ? 1 : 2);
	if (use_clear)
		alert("`zpool clear` to remove some alarm(s)", 0);
	if (alerts.admin_required)
		alert("*Manual*intervention*required*", 2);

	if (alerts.scrubbing || alerts.rebuilding) {
		int seconds = alerts.eta;
		int days, hours, minutes;

		days = seconds / 86400;
		seconds -= days * 86400;

		hours = seconds / 3600;
		seconds -= hours * 3600;

		minutes = seconds/60;

		/* Write-heavy and large pools may exceed 100% during scrub.
		 * that should be handled more gracefully than negative ETA.
		 */
		if (alerts.percent >= 100.00)
			sprintf(buffer, "%s in progress (exceeded 100%%, almost finished)",
			 alerts.rebuilding ? "Rebuild" : "Scrub");
		else
			sprintf(buffer, "%s in progress (%.02f%%, eta %dd %dh %dm)",
			 alerts.rebuilding ? "Rebuild" : "Scrub", alerts.percent * 100.00,
			 days, hours, minutes);
		alert(buffer, alerts.rebuilding ? ((p_ok) ? 0 :1) : 0);
	}

	/* Yeah, errata is bad, but if there's actual pool health problems
	 * then those should be delt with first. Don't heap everything on.
	 */
	if (numalerts == 0 && alerts.errata != 0) {
		alert("Pool errata present, check 'zpool status' for details",
		 p_critical ? 2 : 1);
	}

	/* Pool is healthy, report as such and report cosmetic concerns. */
	if (numalerts == 0) {
		alert("Pool healthy", 0);

		/* Only notify if 'zpool upgrade' is viable if
		 * there's no other concerns.
		 */
		if (alerts.upgrade_available)
			alert("Forward upgrades available",0);
	}
}



/* Get status of a non-data-bearing pool.
 * TODO: validate/fix behaviour with mirrored logs with errors.
 */
static void recurse_status_array(zpool_handle_t *zhp, nvlist_t **nv, int numelements) {
	int i;
	vdev_stat_t *vs;
	uint_t arraysize;

	for (i=0; i < numelements; i++) {
		nvlist_lookup_uint64_array(nv[i], ZPOOL_CONFIG_VDEV_STATS,
		    (uint64_t **)&vs, &arraysize);

		if (currentstate == PSTATE_CACHE) {

			if (vs->vs_aux != VDEV_AUX_NONE)
				alerts.cache_io_errors = 1;

			if (vs->vs_checksum_errors > 0)
				alerts.cache_checksum_errors = 1;
			if (vs->vs_read_errors > 0 || vs->vs_write_errors > 0)
				alerts.cache_io_errors = 1;
		} else {
			/* If not a cache, then IO errors on this disk is quite bad. */
			if (vs->vs_checksum_errors > 0 || vs->vs_read_errors > 0 || vs->vs_write_errors > 0)
				alerts.faulted = 1;
			if (vs->vs_aux != VDEV_AUX_NONE)
				alerts.vdev_degraded = 1;
		}

	}

}


/* Recursively scan the pool configuration looking for problems. Keep track
 * of parent/child device types to feed back problem severity properly.
 */
static void recurse_status_config(zpool_handle_t *zhp, nvlist_t *nv) {
	nvlist_t **child;
	uint_t c, children;
	vdev_stat_t *vs;

	/* Set to true if we're degraded, for later detection.
	 * Only redundant types can properly use this.
	 */
	int pending_degraded = 0;

	/* For use with nvlist_* functions */
	uint64_t u64;
	char *string;

	/* Pull essential stats */
	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		children = 0;

	nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c);


	/* Check this device is present (not missing) */
	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NOT_PRESENT, &u64) == 0) {
		alerts.missing = 1;
		return;
	}


	/* Backup the type state. From here on we can't just 'return'
	 * without restoring this first.
	 */
	printstate laststate = currentstate;

	/* Check the type of the device */
	nvlist_lookup_string(nv, ZPOOL_CONFIG_TYPE, &string);
	if (strcmp(string, VDEV_TYPE_DISK) == 0)
		currentstate = PSTATE_DISK;
	else if (strcmp(string, VDEV_TYPE_RAIDZ) == 0 || strcmp(string, VDEV_TYPE_MIRROR) == 0)
		currentstate = PSTATE_VDEV;
	else if (strcmp(string, VDEV_TYPE_REPLACING) == 0)
		currentstate = PSTATE_REPLACING;
	else if (strcmp(string, VDEV_TYPE_ROOT) == 0)
		currentstate = PSTATE_POOL;
	else
		currentstate = PSTATE_UNKNOWN;

	/* Check the alerts on the vdev. */
	switch (vs->vs_aux) {

	/* This case is bad enough to be its own thing, but
	 * there's no break so it spills into the base case.
	 */
	case VDEV_AUX_ERR_EXCEEDED:
	case VDEV_AUX_IO_FAILURE:
		if (currentstate == PSTATE_VDEV || laststate == PSTATE_POOL)
			alerts.vdev_degraded = 1;
		else
			alerts.read_errors = 1;

	/* Nothing to report, but IO errors get counted */
	case 0:
		if (vs->vs_checksum_errors > 0)
			alerts.checksum_errors = 1;
		if (vs->vs_read_errors > 0)
			alerts.read_errors = 1;
		if (vs->vs_write_errors > 0)
			alerts.write_errors = 1;

		/* Is it a top-level/vdev with errors? */
		if (currentstate == PSTATE_VDEV || laststate == PSTATE_POOL) {
			if (vs->vs_checksum_errors > 0)
					alerts.vdev_checksum_errors = 1;
			if (vs->vs_read_errors > 0 || vs->vs_write_errors)
				alerts.vdev_degraded = 1;
		}
		break;
	case VDEV_AUX_SPLIT_POOL:
		/* Not interesting */
		break;

	case VDEV_AUX_NO_REPLICAS:
		alerts.vdev_faulted = 1;
		break;

	default:
		alerts.other_problems = 1;
		break;
	}

	/* Read the administrative status of the device */
	switch (vs->vs_state) {
	case VDEV_STATE_OFFLINE:
		alerts.offline = 1;
		child_offline = 1;
		break;

	/* If the disk is faulted, check for a rebuild in progress. That affects
	 * how severe the error is.
	 */
	case VDEV_STATE_FAULTED:
		if (currentstate == PSTATE_VDEV || laststate == PSTATE_POOL) {
			alerts.vdev_faulted = 1;
		} else if (currentstate == PSTATE_DISK) {

		        /* A faulted disk itself isn't any good to anyone,
		         * but if we're under a rebuilding object then
		         * I assume one of our sibblings is here to
		         * replace us.
		         */
			if (laststate == PSTATE_REPLACING)
				alerts.faulted_rebuilding = 1;
			else
				alerts.faulted = 1;
			child_faulted = 1;
		} else if (currentstate == PSTATE_POOL) {
		        alerts.pool_failed = 1;
		}
			alerts.other_problems = 1;
		break;

	/* case VDEV_STATE_ONLINE:  // is #define alias for HEALTHY */
	case VDEV_STATE_HEALTHY:
		break;

	case VDEV_STATE_CANT_OPEN:
	case VDEV_STATE_REMOVED:
		alerts.missing = 1;
		break;

	case VDEV_STATE_DEGRADED:
		if (currentstate == PSTATE_VDEV) {
			/* We don't want a degraded mirror/raidz to be an
			 * alarm just yet. Examine the children first.
			 * Rebuilds in progress make this "acceptable".
			 */
			pending_degraded = 1;
		} else if (currentstate == PSTATE_DISK) {
			alerts.faulted = 1;
			child_faulted = 1;
		}
		break;

	default:
		alerts.other_problems = 1;
		break;

	}

	/* Recurse on children */
	for (c = 0; c < children; c++) {
		u64 = B_FALSE;
		/* Skip gaps in the configuration */
		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_HOLE,
		    &u64);
		if (u64)
			continue;

		recurse_status_config(zhp, child[c]);

	}

	if (pending_degraded) {
		if (child_offline && !child_faulted) {
			/* Do nothing! This is "okay" */
		} else {
			alerts.vdev_degraded = 1;
		}
	}

	/* Clean up our mess */
	if (currentstate == PSTATE_VDEV) {
	        child_offline = 0;
	        child_faulted = 0;
	}
	currentstate = laststate;

}

static int
nagios_callback(zpool_handle_t *zhp, void *data)
{
	status_cbdata_t *cbp = data;
	nvlist_t *config, *nvroot;
	char *msgid;
	int reason;
	uint_t c;
	vdev_stat_t *vs;
	zpool_errata_t errata;

	config = zpool_get_config(zhp, NULL);
	reason = zpool_get_status(zhp, &msgid, &errata);

	cbp->cb_count++;

	cbp->cb_first = B_FALSE;

	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);
	verify(nvlist_lookup_uint64_array(nvroot, ZPOOL_CONFIG_VDEV_STATS,
	    (uint64_t **)&vs, &c) == 0);

	strncpy(alerts.poolname, zpool_get_name(zhp), sizeof(alerts.poolname));

	/* Clean out errata alarm first */
	alerts.errata = 0;

	switch (reason) {

	/* Definitely criticals because it causes complete pool failure */
	case ZPOOL_STATUS_MISSING_DEV_NR:    /* missing device with no replicas */
	case ZPOOL_STATUS_CORRUPT_LABEL_NR:  /* bad device label with no replicas */
	case ZPOOL_STATUS_BAD_GUID_SUM:      /* sum of device guids didn't match */
	case ZPOOL_STATUS_CORRUPT_POOL:      /* pool metadata is corrupted */
	case ZPOOL_STATUS_CORRUPT_DATA:      /* data errors in user (meta)data */
	case ZPOOL_STATUS_VERSION_NEWER:     /* newer on-disk version */
	case ZPOOL_STATUS_CORRUPT_CACHE:     /* corrupt /kernel/drv/zpool.cache */
	case ZPOOL_STATUS_BAD_LOG:           /* cannot read log chain(s) */
	case ZPOOL_STATUS_IO_FAILURE_CONTINUE: /* failed I/O, failmode 'continue' */
	case ZPOOL_STATUS_UNSUP_FEAT_READ:   /* unsupported features for read */
	case ZPOOL_STATUS_FAULTED_DEV_NR:    /* faulted device with no replicas */
		alerts.pool_failed = 1;
		break;

	/* Critical, admin access required */
	case ZPOOL_STATUS_IO_FAILURE_WAIT:   /* failed I/O, failmode 'wait' */
		alerts.pool_failed = 1;
		alerts.admin_required = 1;
		break;


	/* Admin access suggested: */
	case ZPOOL_STATUS_HOSTID_MISMATCH:   /* last accessed by another system */
		alerts.admin_required = 1;
		break;


	/* Could be crtical, unless the user downgrades it */
	case ZPOOL_STATUS_MISSING_DEV_R:     /* missing device with replicas */
	case ZPOOL_STATUS_CORRUPT_LABEL_R:   /* bad device label with replicas */
	case ZPOOL_STATUS_FAILING_DEV:       /* device experiencing errors */
	case ZPOOL_STATUS_FAULTED_DEV_R:     /* faulted device with replicas */
		alerts.faulted = 1;
		break;

	case ZPOOL_STATUS_REMOVED_DEV:       /* removed device */
		alerts.missing = 1;
		break;

	/* Errata present! */
	case ZPOOL_STATUS_ERRATA:
		alerts.errata = errata;
		break;

	/*
     * If the pool has unsupported features but can still be opened in
     * read-only mode, its status is ZPOOL_STATUS_UNSUP_FEAT_WRITE. If the
     * pool has unsupported features but cannot be opened at all, its
     * status is ZPOOL_STATUS_UNSUP_FEAT_READ.
     */
	case ZPOOL_STATUS_UNSUP_FEAT_WRITE:  /* unsupported features for write */
		alerts.other_problems = 1;
		break;

	case ZPOOL_STATUS_RESILVERING:       /* device being resilvered */
		alerts.rebuilding = 1;
		break;
    /*
     * These faults have no corresponding message ID.  At the time we are
     * checking the status, the original reason for the FMA fault (I/O or
     * checksum errors) has been lost.
     */

    /*
     * The following are not faults per se, but still an error possibly
     * requiring administrative attention.  There is no corresponding
     * message ID.
     */
	case ZPOOL_STATUS_OFFLINE_DEV:       /* device online */

	/* Ignore these results */
	case ZPOOL_STATUS_VERSION_OLDER:     /* older legacy on-disk version */
	case ZPOOL_STATUS_FEAT_DISABLED:     /* supported features are disabled */
	case ZPOOL_STATUS_OK:
		break;

	default:
		alerts.other_problems= 1;
	}


	if (config != NULL) {
		pool_scan_stat_t *ps = NULL;
		nvlist_t **spares, **l2cache;
		uint_t nspares, nl2cache;
		uint64_t nerr;

		/* Pull scan (scrub/resilver) status. */
		(void) nvlist_lookup_uint64_array(nvroot, ZPOOL_CONFIG_SCAN_STATS, (uint64_t **)&ps, &c);
		if (ps && ps->pss_func != POOL_SCAN_NONE &&
		    ps->pss_func < POOL_SCAN_FUNCS &&  (ps->pss_state != DSS_FINISHED)
		    && ps->pss_state != DSS_CANCELED) {
			time_t start, end;
			uint64_t pass_exam, examined, total, elapsed;
			uint_t rate;
			double fraction_done;

			/* Scans have two sets of parameters: global and local.
			 * Global shows when the thing started, ended, and data to process.
			 * Local stats get cleared/recalculated on pool load/import.
			 * Global stats look good to users, but local stats are needed
			 * for time calculations to be remotely accurate - otherwise while
			 * a pool is down/exported it's considered scanning at 0 bytes/sec.
			 */
			start = ps->pss_start_time;
			end = ps->pss_end_time;

			examined = ps->pss_examined ? ps->pss_examined : 1;
			total = ps->pss_to_examine;
			fraction_done = (double)examined / total;

			/* elapsed time for this pass */
			elapsed = time(NULL) - ps->pss_pass_start;
			elapsed = elapsed ? elapsed : 1;
			pass_exam = ps->pss_pass_exam ? ps->pss_pass_exam : 1;
			rate = pass_exam / elapsed;
			rate = rate ? rate : 1;

			alerts.percent = fraction_done;
			alerts.eta = ((total - examined) / rate);

			/* Scrub or resilver? */
			if (ps->pss_func == POOL_SCAN_SCRUB)
				alerts.scrubbing = 1;
			else if (ps->pss_func == POOL_SCAN_RESILVER)
				alerts.rebuilding = 1;
		}


		/* Read the main pool configuration, get stats and health details */
		currentstate = PSTATE_POOL;
		recurse_status_config(zhp, nvroot);


		/* L2ARC caches (if any) */
		currentstate = PSTATE_CACHE;
		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_L2CACHE,
		    &l2cache, &nl2cache) == 0)
			recurse_status_array(zhp, l2cache, nl2cache);


		/* Spares (if any) */
		currentstate = PSTATE_SPARE;
		if (nvlist_lookup_nvlist_array(nvroot, ZPOOL_CONFIG_SPARES,
		    &spares, &nspares) == 0)
			recurse_status_array(zhp, spares, nspares);

		/* Permanent data errors */
		if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_ERRCOUNT,
		    &nerr) == 0) {

			if (nerr != 0)
				alerts.permanent_data_errors = 1;
		}

	} else {
		alerts.other_problems = 1;
	}

	print_alerts();
	return (0);
}



/*
 * Iterate over each pool for data
 */
int
zpool_do_nagios(int argc, char **argv)
{
	int c;
	int ret;
	status_cbdata_t cb = { 0 };

	while ((c = getopt(argc, argv, "owc")) != -1) {
		switch (c) {
		case 'o':
			p_ok = 1;
			break;
		case 'w':
			p_warning = 1;
			break;
		case 'c':
			p_critical = 1;
			break;
		case '?':
		default:
			printf("Unknown option %c\n", optopt);
			return 3;
		}
	}
	argc -= optind;
	argv += optind;


	cb.cb_first = B_TRUE;

	memset(&alerts, 0, sizeof(alerts));

	ret = for_each_pool(argc, argv, B_TRUE, NULL,
	    nagios_callback, &cb);

	/* Did nothing get found? */
	if (cb.cb_first == B_TRUE) {
		printf("No pools found\n");
		return 3;
	}

	/* TODO: Support a pool list on the commandline, an error if any specified
	 * are not actually imported.
	 */
	printf("\n");
	return (alertlevel);
}

