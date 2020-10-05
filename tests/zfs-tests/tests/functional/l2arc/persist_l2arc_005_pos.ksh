#!/bin/ksh -p
#
# CDDL HEADER START
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#
# CDDL HEADER END
#

#
# Copyright (c) 2020, George Amanakis. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/l2arc/l2arc.cfg
. $STF_SUITE/tests/functional/cli_root/zfs_load-key/zfs_load-key_common.kshlib

#
# DESCRIPTION:
#	Persistent L2ARC restores all written log blocks with encryption
#
# STRATEGY:
#	1. Create pool with a cache device.
#	2. Create a an encrypted ZFS file system.
#	3. Create a random file in the entrypted file system,
#		smaller than the cache device, and random read for 10 sec.
#	4. Export pool.
#	5. Read amount of log blocks written.
#	6. Import pool.
#	7. Mount the encrypted ZFS file system.
#	8. Read amount of log blocks built.
#	9. Compare the two amounts.
#	10. Read the file written in (3) and check if l2_hits in
#		/proc/spl/kstat/zfs/arcstats increased.
#	11. Check if the labels of the L2ARC device are intact.
#

verify_runnable "global"

log_assert "Persistent L2ARC restores all written log blocks with encryption."

function cleanup
{
	if poolexists $TESTPOOL ; then
		destroy_pool $TESTPOOL
	fi

	log_must set_tunable32 L2ARC_NOPREFETCH $noprefetch
}
log_onexit cleanup

# L2ARC_NOPREFETCH is set to 0 to let L2ARC handle prefetches
typeset noprefetch=$(get_tunable L2ARC_NOPREFETCH)
log_must set_tunable32 L2ARC_NOPREFETCH 0

typeset fill_mb=800
typeset cache_sz=$(( 2 * $fill_mb ))
export FILE_SIZE=$(( floor($fill_mb / $NUMJOBS) ))M

log_must truncate -s ${cache_sz}M $VDEV_CACHE

typeset log_blk_start=$(get_arcstat l2_log_blk_writes)

log_must zpool create -f $TESTPOOL $VDEV cache $VDEV_CACHE

log_must eval "echo $PASSPHRASE | zfs create -o encryption=on" \
	"-o keyformat=passphrase $TESTPOOL/$TESTFS1"

log_must fio $FIO_SCRIPTS/mkfiles.fio
log_must fio $FIO_SCRIPTS/random_reads.fio

arcstat_quiescence_noecho l2_size
log_must zpool export $TESTPOOL
arcstat_quiescence_noecho l2_feeds

typeset log_blk_end=$(get_arcstat l2_log_blk_writes)
typeset log_blk_rebuild_start=$(get_arcstat l2_rebuild_log_blks)

log_must zpool import -d $VDIR $TESTPOOL
log_must eval "echo $PASSPHRASE | zfs mount -l $TESTPOOL/$TESTFS1"

typeset l2_hits_start=$(get_arcstat l2_hits)

log_must fio $FIO_SCRIPTS/random_reads.fio
arcstat_quiescence_noecho l2_size

typeset log_blk_rebuild_end=$(arcstat_quiescence_echo l2_rebuild_log_blks)
typeset l2_hits_end=$(get_arcstat l2_hits)

log_must test $(( $log_blk_rebuild_end - $log_blk_rebuild_start )) -eq \
	$(( $log_blk_end - $log_blk_start ))

log_must test $l2_hits_end -gt $l2_hits_start

log_must zpool offline $TESTPOOL $VDEV_CACHE
arcstat_quiescence_noecho l2_size

log_must zdb -lq $VDEV_CACHE

log_must zpool destroy -f $TESTPOOL

log_pass "Persistent L2ARC restores all written log blocks with encryption."
