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
. $STF_SUITE/tests/functional/replacement/replacement.cfg

#
# DESCRIPTION:
#	Failed writes of a sequential rebuild record the rebuild's interval
#	of missing txgs in the new device's DTL. That interval must not
#	change during the rebuild, and must not extend below the DTL.
#
# STRATEGY:
#	1. Write data across many metaslabs and attach a device with a
#	   sequential rebuild, failing every write to it.
#	2. Require both a target write failure and persisted rebuild I/O,
#	   then require the device's DTL to start where it did before the
#	   rebuild.
#	3. Require the rebuild to finish.
#

verify_runnable "global"

function cleanup
{
	zinject -c all >/dev/null 2>&1
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
	log_must set_tunable32 REBUILD_SCRUB_ENABLED $ORIG_SCRUB_ENABLED
	destroy_pool $TESTPOOL1
	log_must set_tunable32 SCAN_SUSPEND_PROGRESS $ORIG_SCAN_SUSPEND
	rm -rf $workdir
}

# Print the first txg of a leaf's missing DTL.
function dtl_start # <leaf>
{
	zpool sync $TESTPOOL1 || log_fail "cannot sync $TESTPOOL1"
	zdb -ddd $TESTPOOL1 | awk -v leaf=$1 '
	    $2 ~ /^\[DTL-/ { selected = ($1 == leaf) }
	    selected && $1 == "missing" {
		split($2, r, /[\[,]/); print r[2]; exit }'
}

function rebuild_issued
{
	# Rebuild I/O belongs to the syncing txg, so this drains it before
	# reading the persistent bytes-issued counter.
	zpool sync $TESTPOOL1 || log_fail "cannot sync $TESTPOOL1"
	zdb -dddd $TESTPOOL1 $top_zap > "$workdir/rebuild" ||
	    log_fail "cannot read saved rebuild state"
	# The eleventh field is the persisted bytes-issued counter.
	awk '$1 == "org.openzfs:vdev_rebuild" { print $11 }' \
	    "$workdir/rebuild"
}

log_assert "A sequential rebuild's failed writes keep its txg interval"
ORIG_SCRUB_ENABLED=$(get_tunable REBUILD_SCRUB_ENABLED) ||
    log_fail "cannot read REBUILD_SCRUB_ENABLED"
ORIG_SCAN_SUSPEND=$(get_tunable SCAN_SUSPEND_PROGRESS) ||
    log_fail "cannot read SCAN_SUSPEND_PROGRESS"
workdir=$(mktemp -d $TEST_BASE_DIR/rebuild_bounds.XXXXXX) ||
    log_fail "cannot create test directory"
log_onexit cleanup

log_must truncate -s 1G $workdir/disk-0 $workdir/disk-1
log_must zpool create -f -o failmode=continue $TESTPOOL1 $workdir/disk-0
log_must zfs create -o compression=off $TESTPOOL1/$TESTFS
mntpnt=$(get_prop mountpoint $TESTPOOL1/$TESTFS)
log_must file_write -o create -f $mntpnt/file -b 1048576 -c 256 -d 65
sync_pool $TESTPOOL1

log_must set_tunable32 REBUILD_SCRUB_ENABLED 0
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must zpool attach -s $TESTPOOL1 $workdir/disk-0 $workdir/disk-1
top_zap=$(zdb -C $TESTPOOL1 |
    awk '/com.delphix:vdev_zap_top:/ { print $2 }')
[[ $top_zap == +([0-9]) ]] || log_fail "missing top vdev ZAP"
start=$(dtl_start $workdir/disk-1)
[[ -n "$start" ]] || log_fail "the new device has no missing DTL"

# Slow the rebuild so it can be held after target writes have failed and
# rebuild I/O has been persisted.
log_must zinject -d $workdir/disk-0 -D 25:1 -T read $TESTPOOL1
id=$(zinject -d $workdir/disk-1 -e io -T write -f 100 $TESTPOOL1 |
    awk '/Added handler/ { print $3 }')
[[ -n "$id" ]] || log_fail "cannot inject write errors"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
typeset -i i failed=0 issued=0
for (( i = 0; i < 600; i++ )); do
	failed=$(zinject | awk -v id=$id '$1 == id { print $NF }')
	if (( failed > 0 )); then
		issued=$(rebuild_issued)
		(( issued > 0 )) && break
	fi
	sleep 0.1
done
(( failed > 0 )) || log_fail "no target write failed"
(( issued > 0 )) || log_fail "no rebuild I/O was persisted"
log_must set_tunable32 SCAN_SUSPEND_PROGRESS 1
log_must is_pool_resilvering $TESTPOOL1
issued=$(rebuild_issued)
(( issued > 0 )) || log_fail "no rebuild I/O was persisted"
log_note "$failed target writes failed; $issued rebuild bytes were issued"
now=$(dtl_start $workdir/disk-1)
log_note "missing DTL starts at txg $now, before the rebuild at txg $start"
(( now == start )) || log_fail "failed writes extended the DTL below $start"

log_must set_tunable32 SCAN_SUSPEND_PROGRESS 0
log_must timeout 600 zpool wait -t resilver $TESTPOOL1
log_must eval "zpool history -i $TESTPOOL1 | grep -q 'rebuild.*complete'"

log_pass "A sequential rebuild's failed writes kept its txg interval"
