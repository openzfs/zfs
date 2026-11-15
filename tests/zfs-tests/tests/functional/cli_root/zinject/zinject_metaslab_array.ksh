#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# https://opensource.org/license/CDDL-1.0.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# A read error on the metaslab array of a vdev must not stop txg sync.
#
# metaslab_sync() reads the metaslab's entry back from the vdev's
# metaslab array only to cross-check it against the in-core space map
# object.  The read is allowed to fail, and a failure used to trip a
# VERIFY in txg_sync.  It must be logged as a pool error instead, and
# the pool must keep syncing.
#
# STRATEGY:
# 1. Create a pool on a file vdev with few metaslabs, write some files
#    in a few datasets, and check that every metaslab has a space map.
# 2. Keep the dbuf caches empty, then export and import the pool, so
#    that the metaslab array blocks are not held in memory.
# 3. Sync once, then inject EIO on every read of the metaslab arrays
#    and flush the ARC.
# 4. Free data and sync, a dataset at a time, each sync with a bounded
#    wait, and check that the injection fired.
# 5. Verify that the pool is not suspended and that the error is logged.
# 6. Clear the injection and the errors, scrub, export and import, and
#    verify that the pool is healthy.
#

verify_runnable "global"

TESTPOOL1=testpool_msarray
VDEV_DIR=$TEST_BASE_DIR/zinject_metaslab_array
VDEV=$VDEV_DIR/vdev
STATUS_OUT=$TEST_BASE_DIR/zinject_metaslab_array.status
ZDB_OUT=$TEST_BASE_DIR/zinject_metaslab_array.zdb
DATASETS="fs1 fs2 fs3"
SYNC_PID=

function cleanup
{
	restore_tunable DBUF_CACHE_MAX_BYTES
	restore_tunable DBUF_METADATA_CACHE_MAX_BYTES
	restore_tunable VDEV_MIN_MS_COUNT

	if [[ -n "$SYNC_PID" ]] && kill -0 $SYNC_PID 2>/dev/null &&
	    [[ "$(kstat_pool $TESTPOOL1 state)" != "SUSPENDED" ]]; then
		# txg sync is stuck, don't hang the cleanup as well.
		log_note "zpool sync is still running, pool left as is"
		return
	fi
	zinject -c all >/dev/null 2>&1
	if poolexists $TESTPOOL1 &&
	    [[ "$(kstat_pool $TESTPOOL1 state)" == "SUSPENDED" ]]; then
		zpool clear $TESTPOOL1
	fi
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -rf $VDEV_DIR $STATUS_OUT $ZDB_OUT
}

#
# A txg that cannot complete blocks "zpool sync" for good, so run it in
# the background and wait for it for at most $1 seconds.
#
function bounded_sync # seconds
{
	typeset -i timeout=$1
	typeset -i i=0

	zpool sync $TESTPOOL1 &
	SYNC_PID=$!
	while ((i < timeout)) && kill -0 $SYNC_PID 2>/dev/null; do
		sleep 1
		((i += 1))
	done
	if kill -0 $SYNC_PID 2>/dev/null; then
		log_note "pool state: $(kstat_pool $TESTPOOL1 state)"
		log_fail "zpool sync did not return within $timeout seconds"
	fi
	wait $SYNC_PID || log_fail "zpool sync failed"
	SYNC_PID=
}

# The INJECT count of the metaslab array injection.
function inject_count
{
	zinject | awk '$5 == "metaslab" { print $NF }'
}

log_assert "A read error on the metaslab array does not stop txg sync"
log_onexit cleanup

log_must save_tunable VDEV_MIN_MS_COUNT
log_must save_tunable DBUF_CACHE_MAX_BYTES
log_must save_tunable DBUF_METADATA_CACHE_MAX_BYTES

log_must mkdir -p $VDEV_DIR
log_must truncate -s $MINVDEVSIZE $VDEV

#
# A metaslab with no space map yet gets one on its first allocation,
# which writes its entry of the metaslab array; that write reads the
# block and suspends the pool when it fails, which is not the case
# tested here.  With few metaslabs, the ditto copies of the MOS soon
# give each of them one: check that they all have one.
#
log_must set_tunable32 VDEV_MIN_MS_COUNT 2
log_must zpool create $TESTPOOL1 $VDEV
log_must restore_tunable VDEV_MIN_MS_COUNT

for ds in $DATASETS; do
	log_must zfs create $TESTPOOL1/$ds
	mntpnt=$(get_prop mountpoint $TESTPOOL1/$ds)
	for i in 1 2 3 4; do
		log_must dd if=/dev/urandom of=$mntpnt/file$i bs=128k count=8
	done
done
log_must zpool sync $TESTPOOL1

log_must eval "zdb -m $TESTPOOL1 > $ZDB_OUT"
typeset -i nms nosm
nms=$(awk '$1 == "metaslab" && $5 == "spacemap"' $ZDB_OUT | wc -l)
nosm=$(awk '$1 == "metaslab" && $5 == "spacemap" && $6 == 0' $ZDB_OUT | wc -l)
if ((nms == 0 || nosm != 0)); then
	cat $ZDB_OUT
	log_fail "$nosm of $nms metaslabs have no space map"
fi
log_note "$nms metaslabs, all with a space map"

#
# zinject -a flushes the ARC, but not the dbuf caches, which could keep
# the metaslab array blocks read by the import.
#
log_must set_tunable64 DBUF_CACHE_MAX_BYTES 0
log_must set_tunable64 DBUF_METADATA_CACHE_MAX_BYTES 0
log_must zpool export $TESTPOOL1
log_must zpool import -d $VDEV_DIR $TESTPOOL1

#
# The first sync after the import also updates the config.  Let it
# complete before the injection, which must not hit it.
#
bounded_sync 60
typeset -i i=0
while ((i < 30)) && (($(kstat dbufstats.cache_size_bytes) != 0)); do
	sleep 1
	((i += 1))
done
log_note "dbuf cache: $(kstat dbufstats.cache_size_bytes) bytes"

log_must zinject -a -t metaslab -e io $TESTPOOL1

for ds in $DATASETS; do
	mntpnt=$(get_prop mountpoint $TESTPOOL1/$ds)
	log_must rm -f $mntpnt/file1 $mntpnt/file2 $mntpnt/file3
	bounded_sync 60
	log_note "$ds freed, injected $(inject_count) times"
done
zinject

typeset -i injected
injected=$(inject_count)
if ((injected == 0)); then
	kstat -g dbufstats
	log_fail "the metaslab array was never read from disk"
fi

log_must test "$(kstat_pool $TESTPOOL1 state)" != "SUSPENDED"
log_must eval "zpool status -v $TESTPOOL1 > $STATUS_OUT 2>&1"
cat $STATUS_OUT
log_mustnot grep -q "^errors: No known data errors" $STATUS_OUT

log_must zinject -c all
log_must restore_tunable DBUF_CACHE_MAX_BYTES
log_must restore_tunable DBUF_METADATA_CACHE_MAX_BYTES
log_must zpool clear $TESTPOOL1
log_must zpool scrub -w $TESTPOOL1
log_must zpool export $TESTPOOL1
log_must zpool import -d $VDEV_DIR $TESTPOOL1
log_must eval "zpool status -x $TESTPOOL1 | grep -q 'is healthy'"

for ds in $DATASETS; do
	mntpnt=$(get_prop mountpoint $TESTPOOL1/$ds)
	log_must dd if=$mntpnt/file4 of=/dev/null bs=128k
done

log_pass "A read error on the metaslab array does not stop txg sync"
