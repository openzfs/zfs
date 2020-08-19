/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2018, Joyent, Inc. All rights reserved.
 */

/*
 * The ZFS/Zone I/O throttle and scheduler attempts to ensure fair access to
 * ZFS I/O resources for each zone.
 *
 * I/O contention can be major pain point on a multi-tenant system. A single
 * zone can issue a stream of I/O operations, usually synchronous writes, which
 * disrupt I/O performance for all other zones. This problem is further
 * exacerbated by ZFS, which buffers all asynchronous writes in a single TXG,
 * a set of blocks which are atomically synced to disk. The process of
 * syncing a TXG can occupy all of a device's I/O bandwidth, thereby starving
 * out any pending read operations.
 *
 * There are two facets to this capability; the throttle and the scheduler.
 *
 * Throttle
 *
 * The requirements on the throttle are:
 *
 *     1) Ensure consistent and predictable I/O latency across all zones.
 *     2) Sequential and random workloads have very different characteristics,
 *        so it is a non-starter to track IOPS or throughput.
 *     3) A zone should be able to use the full disk bandwidth if no other zone
 *        is actively using the disk.
 *
 * The throttle has two components: one to track and account for each zone's
 * I/O requests, and another to throttle each zone's operations when it
 * exceeds its fair share of disk I/O. When the throttle detects that a zone is
 * consuming more than is appropriate, each read or write system call is
 * delayed by up to 100 microseconds, which we've found is sufficient to allow
 * other zones to interleave I/O requests during those delays.
 *
 * Note: The throttle will delay each logical I/O (as opposed to the physical
 * I/O which will likely be issued asynchronously), so it may be easier to
 * think of the I/O throttle delaying each read/write syscall instead of the
 * actual I/O operation. For each zone, the throttle tracks an ongoing average
 * of read and write operations performed to determine the overall I/O
 * utilization for each zone.
 *
 * The throttle calculates a I/O utilization metric for each zone using the
 * following formula:
 *
 *     (# of read syscalls) x (Average read latency) +
 *     (# of write syscalls) x (Average write latency)
 *
 * Once each zone has its utilization metric, the I/O throttle will compare I/O
 * utilization across all zones, and if a zone has a higher-than-average I/O
 * utilization, system calls from that zone are throttled. That is, if one
 * zone has a much higher utilization, that zone's delay is increased by 5
 * microseconds, up to a maximum of 100 microseconds. Conversely, if a zone is
 * already throttled and has a lower utilization than average, its delay will
 * be lowered by 5 microseconds.
 *
 * The throttle calculation is driven by IO activity, but since IO does not
 * happen at fixed intervals, timestamps are used to track when the last update
 * was made and to drive recalculation.
 *
 * The throttle recalculates each zone's I/O usage and throttle delay (if any)
 * on the zfs_zone_adjust_time interval. Overall I/O latency is maintained as
 * a decayed average which is updated on the zfs_zone_sys_avg_cycle interval.
 *
 * Scheduler
 *
 * The I/O scheduler manages the vdev queues – the queues of pending I/Os to
 * issue to the disks. It only makes scheduling decisions for the two
 * synchronous I/O queues (read & write).
 *
 * The scheduler maintains how many I/Os in the queue are from each zone, and
 * if one zone has a disproportionately large number of I/Os in the queue, the
 * scheduler will allow certain I/Os from the underutilized zones to be "bumped"
 * and pulled from the middle of the queue. This bump allows zones with a small
 * number of I/Os (so small they may not even be taken into account by the
 * throttle) to complete quickly instead of waiting behind dozens of I/Os from
 * other zones.
 */

#include <sys/spa.h>
#include <sys/vdev_impl.h>
#include <sys/zfs_zone.h>

#ifndef _KERNEL

/*
 * Stubs for when compiling for user-land.
 */

void
zfs_zone_io_throttle(zfs_zone_iop_type_t type)
{
}

void
zfs_zone_zio_init(zio_t *zp)
{
}

void
zfs_zone_zio_start(zio_t *zp)
{
}

void
zfs_zone_zio_done(zio_t *zp)
{
}

void
zfs_zone_zio_dequeue(zio_t *zp)
{
}

void
zfs_zone_zio_enqueue(zio_t *zp)
{
}

/*ARGSUSED*/
void
zfs_zone_report_txg_sync(void *dp)
{
}

hrtime_t
zfs_zone_txg_delay()
{
	return (MSEC2NSEC(10));
}

#else

/*
 * The real code.
 */

#include <sys/systm.h>
#include <sys/thread.h>
#include <sys/proc.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/atomic.h>
#include <sys/zio.h>
#include <sys/zone.h>
#include <sys/avl.h>
#include <sys/sdt.h>
#include <sys/ddi.h>

/*
 * The zone throttle delays read and write operations from certain zones based
 * on each zone's IO utilitzation.  Once a cycle (defined by zfs_zone_cycle_time
 * below), the delays for each zone are recalculated based on the utilization
 * over the previous window.
 */
boolean_t	zfs_zone_delay_enable = B_TRUE;	/* enable IO throttle */
uint8_t		zfs_zone_delay_step = 5;	/* usec amnt to change delay */
uint8_t		zfs_zone_delay_ceiling = 100;	/* usec delay max */

boolean_t	zfs_zone_priority_enable = B_TRUE;  /* enable IO priority */

/*
 * For certain workloads, one zone may be issuing primarily sequential I/O and
 * another primarily random I/O.  The sequential I/O will complete much more
 * quickly than the random I/O, driving the average system latency for those
 * operations way down.  As a result, the random I/O may be throttled back, even
 * though the sequential I/O should be throttled to allow the random I/O more
 * access to the disk.
 *
 * This tunable limits the discrepancy between the read and write system
 * latency.  If one becomes excessively high, this tunable prevents the I/O
 * throttler from exacerbating the imbalance.
 */
uint_t		zfs_zone_rw_lat_limit = 10;

/*
 * The I/O throttle will only start delaying zones when it detects disk
 * utilization has reached a certain level.  This tunable controls the
 * threshold at which the throttle will start delaying zones.  When the number
 * of vdevs is small, the calculation should correspond closely with the %b
 * column from iostat -- but as the number of vdevs becomes large, it will
 * correlate less and less to any single device (therefore making it a poor
 * approximation for the actual I/O utilization on such systems).  We
 * therefore use our derived utilization conservatively:  we know that low
 * derived utilization does indeed correlate to low I/O use -- but that a high
 * rate of derived utilization does not necesarily alone denote saturation;
 * where we see a high rate of utilization, we also look for laggard I/Os to
 * attempt to detect saturation.
 */
uint_t		zfs_zone_util_threshold = 80;
uint_t		zfs_zone_underutil_threshold = 60;

/*
 * There are three important tunables here:  zfs_zone_laggard_threshold denotes
 * the threshold at which an I/O is considered to be of notably high latency;
 * zfs_zone_laggard_recent denotes the number of microseconds before the
 * current time after which the last laggard is considered to be sufficiently
 * recent to merit increasing the throttle; zfs_zone_laggard_ancient denotes
 * the microseconds before the current time before which the last laggard is
 * considered to be sufficiently old to merit decreasing the throttle.  The
 * most important tunable of these three is the zfs_zone_laggard_threshold: in
 * modeling data from a large public cloud, this tunable was found to have a
 * much greater effect on the throttle than the two time-based thresholds.
 * This must be set high enough to not result in spurious throttling, but not
 * so high as to allow pathological I/O to persist in the system.
 */
uint_t		zfs_zone_laggard_threshold = 50000;	/* 50 ms */
uint_t		zfs_zone_laggard_recent = 1000000;	/* 1000 ms */
uint_t		zfs_zone_laggard_ancient = 5000000;	/* 5000 ms */

/*
 * Throughout this subsystem, our timestamps are in microseconds.  Our system
 * average cycle is one second or 1 million microseconds.  Our zone counter
 * update cycle is two seconds or 2 million microseconds.  We use a longer
 * duration for that cycle because some ops can see a little over two seconds of
 * latency when they are being starved by another zone.
 */
uint_t 		zfs_zone_sys_avg_cycle = 1000000;	/* 1 s */
uint_t 		zfs_zone_cycle_time = 2000000;		/* 2 s */

/*
 * How often the I/O throttle will reevaluate each zone's utilization, in
 * microseconds. Default is 1/4 sec.
 */
uint_t 		zfs_zone_adjust_time = 250000;		/* 250 ms */

typedef struct {
	hrtime_t	cycle_start;
	hrtime_t	cycle_lat;
	hrtime_t	sys_avg_lat;
	uint_t		cycle_cnt;
} sys_lat_cycle_t;

typedef struct {
	hrtime_t zi_now;
	uint_t zi_avgrlat;
	uint_t zi_avgwlat;
	uint64_t zi_totpri;
	uint64_t zi_totutil;
	int zi_active;
	uint_t zi_diskutil;
	boolean_t zi_underutil;
	boolean_t zi_overutil;
} zoneio_stats_t;

static sys_lat_cycle_t	rd_lat;
static sys_lat_cycle_t	wr_lat;

/*
 * Some basic disk stats to determine disk utilization. The utilization info
 * for all disks on the system is aggregated into these values.
 *
 * Overall disk utilization for the current cycle is calculated as:
 *
 * ((zfs_disk_rtime - zfs_disk_last_rtime) * 100)
 * ----------------------------------------------
 *    ((now - zfs_zone_last_checked) * 1000);
 */
kmutex_t	zfs_disk_lock;		/* protects the following: */
uint_t		zfs_disk_rcnt;		/* Number of outstanding IOs */
hrtime_t	zfs_disk_rtime = 0; /* cummulative sum of time performing IO */
hrtime_t	zfs_disk_rlastupdate = 0; /* time last IO dispatched */

hrtime_t	zfs_disk_last_rtime = 0; /* prev. cycle's zfs_disk_rtime val */
/* time that we last updated per-zone throttle info */
kmutex_t	zfs_last_check_lock;	/* protects zfs_zone_last_checked */
hrtime_t	zfs_zone_last_checked = 0;
hrtime_t	zfs_disk_last_laggard = 0;

/*
 * Data used to keep track of how often txg sync is running.
 */
extern int	zfs_txg_timeout;
static uint_t	txg_last_check;
static uint_t	txg_cnt;
static uint_t	txg_sync_rate;

boolean_t	zfs_zone_schedule_enable = B_TRUE;	/* enable IO sched. */
/*
 * Threshold for when zio scheduling should kick in.
 *
 * This threshold is based on the zfs_vdev_sync_read_max_active value for the
 * number of I/Os that can be pending on a device.  If there are more than the
 * max_active ops already queued up, beyond those already issued to the vdev,
 * then use zone-based scheduling to get the next synchronous zio.
 */
uint32_t	zfs_zone_schedule_thresh = 10;

/*
 * On each pass of the scheduler we increment the zone's weight (up to this
 * maximum). The weight is used by the scheduler to prevent starvation so
 * that zones which haven't been able to do any IO over many iterations
 * will max out thier weight to this value.
 */
#define	SCHED_WEIGHT_MAX	20

/*
 * Tunables for delay throttling when TXG sync is occurring.
 *
 * If the zone is performing a write and we're doing above normal TXG syncing,
 * then throttle for longer than normal. The zone's wait time is multiplied
 * by the scale (zfs_zone_txg_throttle_scale).
 */
int		zfs_zone_txg_throttle_scale = 2;
hrtime_t	zfs_zone_txg_delay_nsec = MSEC2NSEC(20);

typedef struct {
	int		zq_qdepth;
	zio_priority_t	zq_queue;
	int		zq_priority;
	int		zq_wt;
	zoneid_t	zq_zoneid;
} zone_q_bump_t;

/*
 * This uses gethrtime() but returns a value in usecs.
 */
#define	GET_USEC_TIME		(gethrtime() / 1000)
#define	NANO_TO_MICRO(x)	(x / (NANOSEC / MICROSEC))

/*
 * Keep track of the zone's ZFS IOPs.
 *
 * See the comment on the zfs_zone_io_throttle function for which/how IOPs are
 * accounted for.
 *
 * If the number of ops is >1 then we can just use that value.  However,
 * if the number of ops is <2 then we might have a zone which is trying to do
 * IO but is not able to get any ops through the system.  We don't want to lose
 * track of this zone so we factor in its decayed count into the current count.
 *
 * Each cycle (zfs_zone_sys_avg_cycle) we want to update the decayed count.
 * However, since this calculation is driven by IO activity and since IO does
 * not happen at fixed intervals, we use a timestamp to see when the last update
 * was made.  If it was more than one cycle ago, then we need to decay the
 * historical count by the proper number of additional cycles in which no IO was
 * performed.
 *
 * Return a time delta indicating how far into the current cycle we are or 0
 * if the last IO was more than a cycle ago.
 */
static hrtime_t
compute_historical_zone_cnt(hrtime_t unow, sys_zio_cntr_t *cp)
{
	hrtime_t delta;
	int	gen_cnt;

	/*
	 * Check if its time to recompute a new zone count.
	 * If we're still collecting data for the current cycle, return false.
	 */
	delta = unow - cp->cycle_start;
	if (delta < zfs_zone_cycle_time)
		return (delta);

	/* A previous cycle is past, compute the new zone count. */

	/*
	 * Figure out how many generations we have to decay the historical
	 * count, since multiple cycles may have elapsed since our last IO.
	 * We depend on int rounding here.
	 */
	gen_cnt = (int)(delta / zfs_zone_cycle_time);

	/* If more than 5 cycles since last the IO, reset count. */
	if (gen_cnt > 5) {
		cp->zone_avg_cnt = 0;
	} else {
		/* Update the count. */
		int	i;

		/*
		 * If the zone did more than 1 IO, just use its current count
		 * as the historical value, otherwise decay the historical
		 * count and factor that into the new historical count.  We
		 * pick a threshold > 1 so that we don't lose track of IO due
		 * to int rounding.
		 */
		if (cp->cycle_cnt > 1)
			cp->zone_avg_cnt = cp->cycle_cnt;
		else
			cp->zone_avg_cnt = cp->cycle_cnt +
			    (cp->zone_avg_cnt / 2);

		/*
		 * If more than one generation has elapsed since the last
		 * update, decay the values further.
		 */
		for (i = 1; i < gen_cnt; i++)
			cp->zone_avg_cnt = cp->zone_avg_cnt / 2;
	}

	/* A new cycle begins. */
	cp->cycle_start = unow;
	cp->cycle_cnt = 0;

	return (0);
}

/*
 * Add IO op data to the zone.
 */
static void
add_zone_iop(zone_persist_t *zpd, hrtime_t unow, zfs_zone_iop_type_t op)
{
	zone_zfs_io_t *iop;

	mutex_enter(&zpd->zpers_zfs_lock);
	iop = zpd->zpers_zfsp;
	if (iop == NULL) {
		mutex_exit(&zpd->zpers_zfs_lock);
		return;
	}

	switch (op) {
	case ZFS_ZONE_IOP_READ:
		(void) compute_historical_zone_cnt(unow, &iop->zpers_rd_ops);
		iop->zpers_rd_ops.cycle_cnt++;
		break;
	case ZFS_ZONE_IOP_WRITE:
		(void) compute_historical_zone_cnt(unow, &iop->zpers_wr_ops);
		iop->zpers_wr_ops.cycle_cnt++;
		break;
	case ZFS_ZONE_IOP_LOGICAL_WRITE:
		(void) compute_historical_zone_cnt(unow, &iop->zpers_lwr_ops);
		iop->zpers_lwr_ops.cycle_cnt++;
		break;
	}
	mutex_exit(&zpd->zpers_zfs_lock);
}

/*
 * Use a decaying average to keep track of the overall system latency.
 *
 * We want to have the recent activity heavily weighted, but if the
 * activity decreases or stops, then the average should quickly decay
 * down to the new value.
 *
 * Each cycle (zfs_zone_sys_avg_cycle) we want to update the decayed average.
 * However, since this calculation is driven by IO activity and since IO does
 * not happen at fixed intervals, we use a timestamp to see when the last
 * update was made. If it was more than one cycle ago, then we need to decay
 * the average by the proper number of additional cycles in which no IO was
 * performed.
 *
 * Return true if we actually computed a new system average.
 * If we're still within an active cycle there is nothing to do, return false.
 */
static boolean_t
compute_new_sys_avg(hrtime_t unow, sys_lat_cycle_t *cp)
{
	hrtime_t delta;
	int	gen_cnt;

	/*
	 * Check if its time to recompute a new average.
	 * If we're still collecting data for the current cycle, return false.
	 */
	delta = unow - cp->cycle_start;
	if (delta < zfs_zone_sys_avg_cycle)
		return (B_FALSE);

	/* A previous cycle is past, compute a new system average. */

	/*
	 * Figure out how many generations we have to decay, since multiple
	 * cycles may have elapsed since our last IO.
	 * We count on int rounding here.
	 */
	gen_cnt = (int)(delta / zfs_zone_sys_avg_cycle);

	/* If more than 5 cycles since last the IO, reset average. */
	if (gen_cnt > 5) {
		cp->sys_avg_lat = 0;
	} else {
		/* Update the average. */
		int	i;

		cp->sys_avg_lat =
		    (cp->sys_avg_lat + cp->cycle_lat) / (1 + cp->cycle_cnt);

		/*
		 * If more than one generation has elapsed since the last
		 * update, decay the values further.
		 */
		for (i = 1; i < gen_cnt; i++)
			cp->sys_avg_lat = cp->sys_avg_lat / 2;
	}

	/* A new cycle begins. */
	cp->cycle_start = unow;
	cp->cycle_cnt = 0;
	cp->cycle_lat = 0;

	return (B_TRUE);
}

static void
add_sys_iop(hrtime_t unow, int op, int lat)
{
	switch (op) {
	case ZFS_ZONE_IOP_READ:
		(void) compute_new_sys_avg(unow, &rd_lat);
		atomic_inc_uint(&rd_lat.cycle_cnt);
		atomic_add_64((uint64_t *)&rd_lat.cycle_lat, (int64_t)lat);
		break;
	case ZFS_ZONE_IOP_WRITE:
		(void) compute_new_sys_avg(unow, &wr_lat);
		atomic_inc_uint(&wr_lat.cycle_cnt);
		atomic_add_64((uint64_t *)&wr_lat.cycle_lat, (int64_t)lat);
		break;
	}
}

/*
 * Get the zone IO counts.
 */
static uint_t
calc_zone_cnt(hrtime_t unow, sys_zio_cntr_t *cp)
{
	hrtime_t delta;
	uint_t cnt;

	if ((delta = compute_historical_zone_cnt(unow, cp)) == 0) {
		/*
		 * No activity in the current cycle, we already have the
		 * historical data so we'll use that.
		 */
		cnt = cp->zone_avg_cnt;
	} else {
		/*
		 * If we're less than half way through the cycle then use
		 * the current count plus half the historical count, otherwise
		 * just use the current count.
		 */
		if (delta < (zfs_zone_cycle_time / 2))
			cnt = cp->cycle_cnt + (cp->zone_avg_cnt / 2);
		else
			cnt = cp->cycle_cnt;
	}

	return (cnt);
}

/*
 * Get the average read/write latency in usecs for the system.
 */
static uint_t
calc_avg_lat(hrtime_t unow, sys_lat_cycle_t *cp)
{
	if (compute_new_sys_avg(unow, cp)) {
		/*
		 * No activity in the current cycle, we already have the
		 * historical data so we'll use that.
		 */
		return (cp->sys_avg_lat);
	} else {
		/*
		 * We're within a cycle; weight the current activity higher
		 * compared to the historical data and use that.
		 */
		DTRACE_PROBE3(zfs__zone__calc__wt__avg,
		    uintptr_t, cp->sys_avg_lat,
		    uintptr_t, cp->cycle_lat,
		    uintptr_t, cp->cycle_cnt);

		return ((cp->sys_avg_lat + (cp->cycle_lat * 8)) /
		    (1 + (cp->cycle_cnt * 8)));
	}
}

/*
 * Account for the current IOP on the zone and for the system as a whole.
 * The latency parameter is in usecs.
 */
static void
add_iop(zone_persist_t *zpd, hrtime_t unow, zfs_zone_iop_type_t op,
    hrtime_t lat)
{
	/* Add op to zone */
	add_zone_iop(zpd, unow, op);

	/* Track system latency */
	if (op != ZFS_ZONE_IOP_LOGICAL_WRITE)
		add_sys_iop(unow, op, lat);
}

/*
 * Calculate and return the total number of read ops, write ops and logical
 * write ops for the given zone.  If the zone has issued operations of any type
 * return a non-zero value, otherwise return 0.
 */
static int
get_zone_io_cnt(hrtime_t unow, zone_zfs_io_t *zpd, uint_t *rops, uint_t *wops,
    uint_t *lwops)
{
	ASSERT3P(zpd, !=, NULL);

	*rops = calc_zone_cnt(unow, &zpd->zpers_rd_ops);
	*wops = calc_zone_cnt(unow, &zpd->zpers_wr_ops);
	*lwops = calc_zone_cnt(unow, &zpd->zpers_lwr_ops);

	DTRACE_PROBE4(zfs__zone__io__cnt, uintptr_t, zpd,
	    uintptr_t, *rops, uintptr_t, *wops, uintptr_t, *lwops);

	return (*rops | *wops | *lwops);
}

/*
 * Get the average read/write latency in usecs for the system.
 */
static void
get_sys_avg_lat(hrtime_t unow, uint_t *rlat, uint_t *wlat)
{
	*rlat = calc_avg_lat(unow, &rd_lat);
	*wlat = calc_avg_lat(unow, &wr_lat);

	/*
	 * In an attempt to improve the accuracy of the throttling algorithm,
	 * assume that IO operations can't have zero latency.  Instead, assume
	 * a reasonable lower bound for each operation type. If the actual
	 * observed latencies are non-zero, use those latency values instead.
	 */
	if (*rlat == 0)
		*rlat = 1000;
	if (*wlat == 0)
		*wlat = 1000;

	DTRACE_PROBE2(zfs__zone__sys__avg__lat, uintptr_t, *rlat,
	    uintptr_t, *wlat);
}

/*
 * Find disk utilization for each zone and average utilization for all active
 * zones.
 */
static int
zfs_zone_wait_adjust_calculate_cb(zone_t *zonep, void *arg)
{
	zoneio_stats_t *sp = arg;
	uint_t rops, wops, lwops;
	zone_persist_t *zpd = &zone_pdata[zonep->zone_id];
	zone_zfs_io_t *iop = zpd->zpers_zfsp;

	ASSERT3P(iop, !=, NULL);

	mutex_enter(&zpd->zpers_zfs_lock);
	if (zonep->zone_id == GLOBAL_ZONEID ||
	    get_zone_io_cnt(sp->zi_now, iop, &rops, &wops, &lwops) == 0) {
		mutex_exit(&zpd->zpers_zfs_lock);
		return (0);
	}

	iop->zpers_io_util = (rops * sp->zi_avgrlat) + (wops * sp->zi_avgwlat) +
	    (lwops * sp->zi_avgwlat);
	sp->zi_totutil += iop->zpers_io_util;

	if (iop->zpers_io_util > 0) {
		sp->zi_active++;
		sp->zi_totpri += iop->zpers_zfs_io_pri;
	}

	/*
	 * sdt:::zfs-zone-utilization
	 *
	 *	arg0: zone ID
	 *	arg1: read operations observed during time window
	 *	arg2: physical write operations observed during time window
	 *	arg3: logical write ops observed during time window
	 *	arg4: calculated utilization given read and write ops
	 *	arg5: I/O priority assigned to this zone
	 */
	DTRACE_PROBE6(zfs__zone__utilization, uint_t, zonep->zone_id,
	    uint_t, rops, uint_t, wops, uint_t, lwops,
	    uint64_t, iop->zpers_io_util, uint16_t, iop->zpers_zfs_io_pri);

	mutex_exit(&zpd->zpers_zfs_lock);

	return (0);
}

static void
zfs_zone_delay_inc(zone_zfs_io_t *zpd)
{
	ASSERT3P(zpd, !=, NULL);

	if (zpd->zpers_io_delay < zfs_zone_delay_ceiling)
		zpd->zpers_io_delay += zfs_zone_delay_step;
}

static void
zfs_zone_delay_dec(zone_zfs_io_t *zpd)
{
	ASSERT3P(zpd, !=, NULL);

	if (zpd->zpers_io_delay > 0)
		zpd->zpers_io_delay -= zfs_zone_delay_step;
}

/*
 * For all zones "far enough" away from the average utilization, increase that
 * zones delay.  Otherwise, reduce its delay.
 */
static int
zfs_zone_wait_adjust_delay_cb(zone_t *zonep, void *arg)
{
	zone_persist_t *zpd = &zone_pdata[zonep->zone_id];
	zone_zfs_io_t *iop = zpd->zpers_zfsp;
	zoneio_stats_t *sp = arg;
	uint8_t delay;
	uint_t fairutil = 0;

	ASSERT3P(iop, !=, NULL);

	mutex_enter(&zpd->zpers_zfs_lock);
	delay = iop->zpers_io_delay;
	iop->zpers_io_util_above_avg = 0;

	/*
	 * Given the calculated total utilitzation for all zones, calculate the
	 * fair share of I/O for this zone.
	 */
	if (zfs_zone_priority_enable && sp->zi_totpri > 0) {
		fairutil = (sp->zi_totutil * iop->zpers_zfs_io_pri) /
		    sp->zi_totpri;
	} else if (sp->zi_active > 0) {
		fairutil = sp->zi_totutil / sp->zi_active;
	}

	/*
	 * Adjust each IO's delay.  If the overall delay becomes too high, avoid
	 * increasing beyond the ceiling value.
	 */
	if (iop->zpers_io_util > fairutil && sp->zi_overutil) {
		iop->zpers_io_util_above_avg = 1;

		if (sp->zi_active > 1)
			zfs_zone_delay_inc(iop);
	} else if (iop->zpers_io_util < fairutil || sp->zi_underutil ||
	    sp->zi_active <= 1) {
		zfs_zone_delay_dec(iop);
	}

	/*
	 * sdt:::zfs-zone-throttle
	 *
	 *	arg0: zone ID
	 *	arg1: old delay for this zone
	 *	arg2: new delay for this zone
	 *	arg3: calculated fair I/O utilization
	 *	arg4: actual I/O utilization
	 */
	DTRACE_PROBE5(zfs__zone__throttle, uintptr_t, zonep->zone_id,
	    uintptr_t, delay, uintptr_t, iop->zpers_io_delay,
	    uintptr_t, fairutil, uintptr_t, iop->zpers_io_util);

	mutex_exit(&zpd->zpers_zfs_lock);

	return (0);
}

/*
 * Examine the utilization between different zones, and adjust the delay for
 * each zone appropriately.
 */
static void
zfs_zone_wait_adjust(hrtime_t unow, hrtime_t last_checked)
{
	zoneio_stats_t stats;
	hrtime_t laggard_udelta = 0;

	(void) bzero(&stats, sizeof (stats));

	stats.zi_now = unow;
	get_sys_avg_lat(unow, &stats.zi_avgrlat, &stats.zi_avgwlat);

	if (stats.zi_avgrlat > stats.zi_avgwlat * zfs_zone_rw_lat_limit)
		stats.zi_avgrlat = stats.zi_avgwlat * zfs_zone_rw_lat_limit;
	else if (stats.zi_avgrlat * zfs_zone_rw_lat_limit < stats.zi_avgwlat)
		stats.zi_avgwlat = stats.zi_avgrlat * zfs_zone_rw_lat_limit;

	if (zone_walk(zfs_zone_wait_adjust_calculate_cb, &stats) != 0)
		return;

	/*
	 * Calculate disk utilization for the most recent period.
	 */
	if (zfs_disk_last_rtime == 0 || unow - last_checked <= 0) {
		stats.zi_diskutil = 0;
	} else {
		stats.zi_diskutil =
		    ((zfs_disk_rtime - zfs_disk_last_rtime) * 100) /
		    ((unow - last_checked) * 1000);
	}
	zfs_disk_last_rtime = zfs_disk_rtime;

	if (unow > zfs_disk_last_laggard)
		laggard_udelta = unow - zfs_disk_last_laggard;

	/*
	 * To minimize porpoising, we have three separate states for our
	 * assessment of I/O performance:  overutilized, underutilized, and
	 * neither overutilized nor underutilized.  We will increment the
	 * throttle if a zone is using more than its fair share _and_ I/O
	 * is overutilized; we will decrement the throttle if a zone is using
	 * less than its fair share _or_ I/O is underutilized.
	 */
	stats.zi_underutil = stats.zi_diskutil < zfs_zone_underutil_threshold ||
	    laggard_udelta > zfs_zone_laggard_ancient;

	stats.zi_overutil = stats.zi_diskutil > zfs_zone_util_threshold &&
	    laggard_udelta < zfs_zone_laggard_recent;

	/*
	 * sdt:::zfs-zone-stats
	 *
	 * Statistics observed over the last period:
	 *
	 *	arg0: average system read latency
	 *	arg1: average system write latency
	 *	arg2: number of active zones
	 *	arg3: total I/O 'utilization' for all zones
	 *	arg4: total I/O priority of all active zones
	 *	arg5: calculated disk utilization
	 */
	DTRACE_PROBE6(zfs__zone__stats, uintptr_t, stats.zi_avgrlat,
	    uintptr_t, stats.zi_avgwlat, uintptr_t, stats.zi_active,
	    uintptr_t, stats.zi_totutil, uintptr_t, stats.zi_totpri,
	    uintptr_t, stats.zi_diskutil);

	(void) zone_walk(zfs_zone_wait_adjust_delay_cb, &stats);
}

/*
 * Callback used to calculate a zone's IO schedule priority.
 *
 * We scan the zones looking for ones with ops in the queue.  Out of those,
 * we pick the one that calculates to the highest schedule priority.
 */
static int
get_sched_pri_cb(zone_t *zonep, void *arg)
{
	int pri;
	uint_t cnt;
	zone_q_bump_t *qbp = arg;
	zio_priority_t p = qbp->zq_queue;
	zone_persist_t *zpd = &zone_pdata[zonep->zone_id];
	zone_zfs_io_t *iop;

	mutex_enter(&zpd->zpers_zfs_lock);
	iop = zpd->zpers_zfsp;
	if (iop == NULL) {
		mutex_exit(&zpd->zpers_zfs_lock);
		return (0);
	}

	cnt = iop->zpers_zfs_queued[p];
	if (cnt == 0) {
		iop->zpers_zfs_weight = 0;
		mutex_exit(&zpd->zpers_zfs_lock);
		return (0);
	}

	/*
	 * On each pass, increment the zone's weight.  We use this as input
	 * to the calculation to prevent starvation.  The value is reset
	 * each time we issue an IO for this zone so zones which haven't
	 * done any IO over several iterations will see their weight max
	 * out.
	 */
	if (iop->zpers_zfs_weight < SCHED_WEIGHT_MAX)
		iop->zpers_zfs_weight++;

	/*
	 * This zone's IO priority is the inverse of the number of IOs
	 * the zone has enqueued * zone's configured priority * weight.
	 * The queue depth has already been scaled by 10 to avoid problems
	 * with int rounding.
	 *
	 * This means that zones with fewer IOs in the queue will get
	 * preference unless other zone's assigned priority pulls them
	 * ahead.  The weight is factored in to help ensure that zones
	 * which haven't done IO in a while aren't getting starved.
	 */
	pri = (qbp->zq_qdepth / cnt) *
	    iop->zpers_zfs_io_pri * iop->zpers_zfs_weight;

	/*
	 * If this zone has a higher priority than what we found so far,
	 * it becomes the new leading contender.
	 */
	if (pri > qbp->zq_priority) {
		qbp->zq_zoneid = zonep->zone_id;
		qbp->zq_priority = pri;
		qbp->zq_wt = iop->zpers_zfs_weight;
	}
	mutex_exit(&zpd->zpers_zfs_lock);
	return (0);
}

/*
 * See if we need to bump a zone's zio to the head of the queue. This is only
 * done on the two synchronous I/O queues (see the block comment on the
 * zfs_zone_schedule function). We get the correct vdev_queue_class_t and
 * queue depth from our caller.
 *
 * For single-threaded synchronous processes a zone cannot get more than
 * 1 op into the queue at a time unless the zone is running multiple processes
 * in parallel.  This can cause an imbalance in performance if there are zones
 * with many parallel processes (and ops in the queue) vs. other zones which
 * are doing simple single-threaded processes, such as interactive tasks in the
 * shell.  These zones can get backed up behind a deep queue and their IO
 * performance will appear to be very poor as a result.  This can make the
 * zone work badly for interactive behavior.
 *
 * The scheduling algorithm kicks in once we start to get a deeper queue.
 * Once that occurs, we look at all of the zones to see which one calculates
 * to the highest priority.  We bump that zone's first zio to the head of the
 * queue.
 *
 * We use a counter on the zone so that we can quickly find how many ops each
 * zone has in the queue without having to search the entire queue itself.
 * This scales better since the number of zones is expected to be on the
 * order of 10-100 whereas the queue depth can be in the range of 50-2000.
 * In addition, since the zio's in the queue only have the zoneid, we would
 * have to look up the zone for each zio enqueued and that means the overhead
 * for scanning the queue each time would be much higher.
 *
 * In all cases, we fall back to simply pulling the next op off the queue
 * if something should go wrong.
 */
static zio_t *
get_next_zio(vdev_queue_class_t *vqc, int qdepth, zio_priority_t p,
    avl_tree_t *tree)
{
	zone_q_bump_t qbump;
	zio_t *zp = NULL, *zphead;
	int cnt = 0;

	/* To avoid problems with int rounding, scale the queue depth by 10 */
	qbump.zq_qdepth = qdepth * 10;
	qbump.zq_priority = 0;
	qbump.zq_zoneid = 0;
	qbump.zq_queue = p;
	(void) zone_walk(get_sched_pri_cb, &qbump);

	zphead = avl_first(tree);

	/* Check if the scheduler didn't pick a zone for some reason!? */
	if (qbump.zq_zoneid != 0) {
		for (zp = avl_first(tree); zp != NULL;
		    zp = avl_walk(tree, zp, AVL_AFTER)) {
			if (zp->io_zoneid == qbump.zq_zoneid)
				break;
			cnt++;
		}
	}

	if (zp == NULL) {
		zp = zphead;
	} else if (zp != zphead) {
		/*
		 * Only fire the probe if we actually picked a different zio
		 * than the one already at the head of the queue.
		 */
		DTRACE_PROBE4(zfs__zone__sched__bump, uint_t, zp->io_zoneid,
		    uint_t, cnt, int, qbump.zq_priority, int, qbump.zq_wt);
	}

	return (zp);
}

/*
 * Add our zone ID to the zio so we can keep track of which zones are doing
 * what, even when the current thread processing the zio is not associated
 * with the zone (e.g. the kernel taskq which pushes out TX groups).
 */
void
zfs_zone_zio_init(zio_t *zp)
{
	zone_t	*zonep = curzone;

	zp->io_zoneid = zonep->zone_id;
}

/*
 * Track and throttle IO operations per zone. Called from:
 *   - dmu_tx_count_write for (logical) write ops (both dataset and zvol writes
 *     go through this path)
 *   - arc_read for read ops that miss the ARC (both dataset and zvol)
 * For each operation, increment that zone's counter based on the type of
 * operation, then delay the operation, if necessary.
 *
 * There are three basic ways that we can see write ops:
 * 1) An application does write syscalls.  Those ops go into a TXG which
 *    we'll count here.  Sometime later a kernel taskq thread (we'll see the
 *    vdev IO as zone 0) will perform some number of physical writes to commit
 *    the TXG to disk.  Those writes are not associated with the zone which
 *    made the write syscalls and the number of operations is not correlated
 *    between the taskq and the zone. We only see logical writes in this
 *    function, we see the physcial writes in the zfs_zone_zio_start and
 *    zfs_zone_zio_done functions.
 * 2) An application opens a file with O_SYNC.  Each write will result in
 *    an operation which we'll see here plus a low-level vdev write from
 *    that zone.
 * 3) An application does write syscalls followed by an fsync().  We'll
 *    count the writes going into a TXG here.  We'll also see some number
 *    (usually much smaller, maybe only 1) of low-level vdev writes from this
 *    zone when the fsync is performed, plus some other low-level vdev writes
 *    from the taskq in zone 0 (are these metadata writes?).
 *
 * 4) In addition to the above, there are misc. system-level writes, such as
 *    writing out dirty pages to swap, or sync(2) calls, which will be handled
 *    by the global zone and which we count but don't generally worry about.
 *
 * Because of the above, we can see writes twice; first because this function
 * is always called by a zone thread for logical writes, but then we also will
 * count the physical writes that are performed at a low level via
 * zfs_zone_zio_start. Without this, it can look like a non-global zone never
 * writes (case 1). Depending on when the TXG is synced, the counts may be in
 * the same sample bucket or in a different one.
 *
 * Tracking read operations is simpler due to their synchronous semantics.  The
 * zfs_read function -- called as a result of a read(2) syscall -- will always
 * retrieve the data to be read through arc_read and we only come into this
 * function when we have an arc miss.
 */
void
zfs_zone_io_throttle(zfs_zone_iop_type_t type)
{
	zoneid_t zid = curzone->zone_id;
	zone_persist_t *zpd = &zone_pdata[zid];
	zone_zfs_io_t *iop;
	hrtime_t unow;
	uint16_t wait;

	unow = GET_USEC_TIME;

	/*
	 * Only bump the counter for logical writes here.  The counters for
	 * tracking physical IO operations are handled in zfs_zone_zio_done.
	 */
	if (type == ZFS_ZONE_IOP_LOGICAL_WRITE) {
		add_iop(zpd, unow, type, 0);
	}

	if (!zfs_zone_delay_enable)
		return;

	mutex_enter(&zpd->zpers_zfs_lock);
	iop = zpd->zpers_zfsp;
	if (iop == NULL) {
		mutex_exit(&zpd->zpers_zfs_lock);
		return;
	}

	/*
	 * If the zone's I/O priority is set to zero, don't throttle that zone's
	 * operations at all.
	 */
	if (iop->zpers_zfs_io_pri == 0) {
		mutex_exit(&zpd->zpers_zfs_lock);
		return;
	}

	/* Handle periodically updating the per-zone I/O parameters */
	if ((unow - zfs_zone_last_checked) > zfs_zone_adjust_time) {
		hrtime_t last_checked;
		boolean_t do_update = B_FALSE;

		/* Recheck under mutex */
		mutex_enter(&zfs_last_check_lock);
		last_checked = zfs_zone_last_checked;
		if ((unow - last_checked) > zfs_zone_adjust_time) {
			zfs_zone_last_checked = unow;
			do_update = B_TRUE;
		}
		mutex_exit(&zfs_last_check_lock);

		if (do_update) {
			mutex_exit(&zpd->zpers_zfs_lock);

			zfs_zone_wait_adjust(unow, last_checked);

			mutex_enter(&zpd->zpers_zfs_lock);
			iop = zpd->zpers_zfsp;
			if (iop == NULL) {
				mutex_exit(&zpd->zpers_zfs_lock);
				return;
			}
		}
	}

	wait = iop->zpers_io_delay;
	mutex_exit(&zpd->zpers_zfs_lock);

	if (wait > 0) {
		/*
		 * If this is a write and we're doing above normal TXG
		 * syncing, then throttle for longer than normal.
		 */
		if (type == ZFS_ZONE_IOP_LOGICAL_WRITE &&
		    (txg_cnt > 1 || txg_sync_rate > 1))
			wait *= zfs_zone_txg_throttle_scale;

		/*
		 * sdt:::zfs-zone-wait
		 *
		 *	arg0: zone ID
		 *	arg1: type of IO operation
		 *	arg2: time to delay (in us)
		 */
		DTRACE_PROBE3(zfs__zone__wait, uintptr_t, zid,
		    uintptr_t, type, uintptr_t, wait);

		drv_usecwait(wait);

		if (curzone->zone_vfs_stats != NULL) {
			atomic_inc_64(&curzone->zone_vfs_stats->
			    zv_delay_cnt.value.ui64);
			atomic_add_64(&curzone->zone_vfs_stats->
			    zv_delay_time.value.ui64, wait);
		}
	}
}

/*
 * XXX Ignore the pool pointer parameter for now.
 *
 * Keep track to see if the TXG sync rate is running above the expected rate.
 * If so, this implies that we are filling TXG's at a high rate due to a heavy
 * write workload.  We use this as input into the zone throttle.
 *
 * This function is called every 5 seconds (zfs_txg_timeout) under a normal
 * write load.  In this case, the sync rate is going to be 1.  When there
 * is a heavy write load, TXG's fill up fast and the sync thread will write
 * the TXG more frequently (perhaps once a second).  In this case the rate
 * will be > 1.  The sync rate is a lagging indicator since it can be up
 * to 5 seconds old.  We use the txg_cnt to keep track of the rate in the
 * current 5 second interval and txg_sync_rate to keep track of the previous
 * 5 second interval.  In that way we don't have a period (1 or more seconds)
 * where the txg_cnt == 0 and we cut back on throttling even though the rate
 * is still high.
 */
/*ARGSUSED*/
void
zfs_zone_report_txg_sync(void *dp)
{
	uint_t now;

	txg_cnt++;
	now = (uint_t)(gethrtime() / NANOSEC);
	if ((now - txg_last_check) >= zfs_txg_timeout) {
		txg_sync_rate = txg_cnt / 2;
		txg_cnt = 0;
		txg_last_check = now;
	}
}

hrtime_t
zfs_zone_txg_delay()
{
	zone_persist_t *zpd = &zone_pdata[curzone->zone_id];
	zone_zfs_io_t *iop;
	uint8_t above;

	mutex_enter(&zpd->zpers_zfs_lock);
	iop = zpd->zpers_zfsp;
	if (iop == NULL) {
		mutex_exit(&zpd->zpers_zfs_lock);
		return (0);
	}

	above = iop->zpers_io_util_above_avg;
	mutex_exit(&zpd->zpers_zfs_lock);

	if (above) {
		return (zfs_zone_txg_delay_nsec);
	}

	return (MSEC2NSEC(10));
}

/*
 * Called from vdev_disk_io_start when an IO hits the end of the zio pipeline
 * and is issued.
 * Keep track of start time for latency calculation in zfs_zone_zio_done.
 */
void
zfs_zone_zio_start(zio_t *zp)
{
	zone_persist_t *zpd = &zone_pdata[zp->io_zoneid];
	zone_zfs_io_t *iop;

	/*
	 * I/Os of type ZIO_TYPE_IOCTL are used to flush the disk cache, not for
	 * an actual I/O operation.  Ignore those operations as they relate to
	 * throttling and scheduling.
	 */
	if (zp->io_type == ZIO_TYPE_IOCTL)
		return;

	mutex_enter(&zpd->zpers_zfs_lock);
	iop = zpd->zpers_zfsp;
	if (iop != NULL) {
		if (zp->io_type == ZIO_TYPE_READ)
			kstat_runq_enter(&iop->zpers_zfs_rwstats);
		iop->zpers_zfs_weight = 0;
	}
	mutex_exit(&zpd->zpers_zfs_lock);

	mutex_enter(&zfs_disk_lock);
	zp->io_dispatched = gethrtime();

	if (zfs_disk_rcnt++ != 0)
		zfs_disk_rtime += (zp->io_dispatched - zfs_disk_rlastupdate);
	zfs_disk_rlastupdate = zp->io_dispatched;
	mutex_exit(&zfs_disk_lock);
}

/*
 * Called from vdev_disk_io_done when an IO completes.
 * Increment our counter for zone ops.
 * Calculate the IO latency avg. for this zone.
 */
void
zfs_zone_zio_done(zio_t *zp)
{
	zone_persist_t *zpd;
	zone_zfs_io_t *iop;
	hrtime_t now, unow, udelta;

	if (zp->io_type == ZIO_TYPE_IOCTL)
		return;

	if (zp->io_dispatched == 0)
		return;

	zpd = &zone_pdata[zp->io_zoneid];

	now = gethrtime();
	unow = NANO_TO_MICRO(now);
	udelta = unow - NANO_TO_MICRO(zp->io_dispatched);

	mutex_enter(&zpd->zpers_zfs_lock);
	iop = zpd->zpers_zfsp;
	if (iop != NULL) {
		/*
		 * To calculate the wsvc_t average, keep a cumulative sum of
		 * all the wait time before each I/O was dispatched. Since most
		 * writes are asynchronous, only track the wait time for
		 * read I/Os.
		 */
		if (zp->io_type == ZIO_TYPE_READ) {
			iop->zpers_zfs_rwstats.reads++;
			iop->zpers_zfs_rwstats.nread += zp->io_size;
			iop->zpers_zfs_rd_waittime +=
			    zp->io_dispatched - zp->io_timestamp;
			kstat_runq_exit(&iop->zpers_zfs_rwstats);
		} else {
			iop->zpers_zfs_rwstats.writes++;
			iop->zpers_zfs_rwstats.nwritten += zp->io_size;
		}
	}
	mutex_exit(&zpd->zpers_zfs_lock);

	mutex_enter(&zfs_disk_lock);
	zfs_disk_rcnt--;
	zfs_disk_rtime += (now - zfs_disk_rlastupdate);
	zfs_disk_rlastupdate = now;

	if (udelta > zfs_zone_laggard_threshold)
		zfs_disk_last_laggard = unow;

	mutex_exit(&zfs_disk_lock);

	if (zfs_zone_delay_enable) {
		add_iop(zpd, unow, zp->io_type == ZIO_TYPE_READ ?
		    ZFS_ZONE_IOP_READ : ZFS_ZONE_IOP_WRITE, udelta);
	}

	/*
	 * sdt:::zfs-zone-latency
	 *
	 *	arg0: zone ID
	 *	arg1: type of I/O operation
	 *	arg2: I/O latency (in us)
	 */
	DTRACE_PROBE3(zfs__zone__latency, uintptr_t, zp->io_zoneid,
	    uintptr_t, zp->io_type, uintptr_t, udelta);
}

void
zfs_zone_zio_dequeue(zio_t *zp)
{
	zio_priority_t p;
	zone_persist_t *zpd = &zone_pdata[zp->io_zoneid];
	zone_zfs_io_t *iop;

	p = zp->io_priority;
	if (p != ZIO_PRIORITY_SYNC_READ && p != ZIO_PRIORITY_SYNC_WRITE)
		return;

	/* We depend on p being defined as either 0 or 1 */
	ASSERT(p < 2);

	mutex_enter(&zpd->zpers_zfs_lock);
	iop = zpd->zpers_zfsp;
	if (iop != NULL) {
		ASSERT(iop->zpers_zfs_queued[p] > 0);
		if (iop->zpers_zfs_queued[p] == 0) {
			cmn_err(CE_WARN, "zfs_zone_zio_dequeue: count==0");
		} else {
			iop->zpers_zfs_queued[p]--;
		}
	}
	mutex_exit(&zpd->zpers_zfs_lock);
}

void
zfs_zone_zio_enqueue(zio_t *zp)
{
	zio_priority_t p;
	zone_persist_t *zpd = &zone_pdata[zp->io_zoneid];
	zone_zfs_io_t *iop;

	p = zp->io_priority;
	if (p != ZIO_PRIORITY_SYNC_READ && p != ZIO_PRIORITY_SYNC_WRITE)
		return;

	/* We depend on p being defined as either 0 or 1 */
	ASSERT(p < 2);

	mutex_enter(&zpd->zpers_zfs_lock);
	iop = zpd->zpers_zfsp;
	if (iop != NULL) {
		iop->zpers_zfs_queued[p]++;
	}
	mutex_exit(&zpd->zpers_zfs_lock);
}

/*
 * Called from vdev_queue_io_to_issue. That function is where zio's are listed
 * in FIFO order on one of the sync queues, then pulled off (by
 * vdev_queue_io_remove) and issued.  We potentially do zone-based scheduling
 * here to find a zone's zio deeper in the sync queue and issue that instead
 * of simply doing FIFO.
 *
 * We only do zone-based zio scheduling for the two synchronous I/O queues
 * (read & write). These queues are normally serviced in FIFO order but we
 * may decide to move a zone's zio to the head of the line. A typical I/O
 * load will be mostly synchronous reads and some asynchronous writes (which
 * are scheduled differently due to transaction groups). There will also be
 * some synchronous writes for those apps which want to ensure their data is on
 * disk. We want to make sure that a zone with a single-threaded app (e.g. the
 * shell) that is doing synchronous I/O (typically reads) isn't penalized by
 * other zones which are doing lots of synchronous I/O because they have many
 * running threads.
 *
 * The vq->vq_lock mutex is held when we're executing this function so we
 * can safely access the "last zone" variable on the queue.
 */
zio_t *
zfs_zone_schedule(vdev_queue_t *vq, zio_priority_t p, avl_index_t idx,
    avl_tree_t *tree)
{
	vdev_queue_class_t *vqc = &vq->vq_class[p];
	uint_t cnt;
	zoneid_t last_zone;
	zio_t *zio;

	ASSERT(MUTEX_HELD(&vq->vq_lock));

	/* Don't change the order on the LBA ordered queues. */
	if (p != ZIO_PRIORITY_SYNC_READ && p != ZIO_PRIORITY_SYNC_WRITE)
		return (avl_nearest(tree, idx, AVL_AFTER));

	/* We depend on p being defined as either 0 or 1 */
	ASSERT(p < 2);

	cnt = avl_numnodes(tree);
	last_zone = vq->vq_last_zone_id;

	/*
	 * If there are only a few zios in the queue then just issue the head.
	 * If there are more than a few zios already queued up, then use
	 * scheduling to get the next zio.
	 */
	if (!zfs_zone_schedule_enable || cnt < zfs_zone_schedule_thresh)
		zio = avl_nearest(tree, idx, AVL_AFTER);
	else
		zio = get_next_zio(vqc, cnt, p, tree);

	vq->vq_last_zone_id = zio->io_zoneid;

	/*
	 * Probe with 4 args; the number of IOs in the queue, the zone that
	 * was last scheduled off this queue, the zone that was associated
	 * with the next IO that is scheduled, and which queue (priority).
	 */
	DTRACE_PROBE4(zfs__zone__sched, uint_t, cnt, uint_t, last_zone,
	    uint_t, zio->io_zoneid, uint_t, p);

	return (zio);
}

#endif
