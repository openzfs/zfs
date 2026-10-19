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
# A scrub started on a raidz or dRAID pool before the resilver of a
# returned device must repair what that device missed.  The scrub is
# limited to the missing txgs and clears the device's DTL when it
# completes, so anything it does not repair stays stale until a later
# full scrub.
#
# STRATEGY:
# For raidz1, raidz2, raidz3 and draid1:
# 1. Create the pool and write some data.
# 2. Export the pool, move the first device away, import it degraded.
# 3. Write more data while the device is away.
# 4. Export the pool, move the device back, import it and scrub at once.
# 5. If a resilver started first, try again (up to 5 times).
# 6. Verify the scrub repaired something, then run a full scrub and
#    verify it repairs nothing on the device.
#

verify_runnable "global"

TESTDIR2="$TEST_BASE_DIR/zpool_scrub_before_resilver"
VDEVDIR="$TEST_BASE_DIR/zpool_scrub_before_resilver.d"
AWAYDIR="$TEST_BASE_DIR/zpool_scrub_before_resilver.away"

function cleanup
{
	poolexists $TESTPOOL2 && destroy_pool $TESTPOOL2
	rm -rf $VDEVDIR $AWAYDIR $TESTDIR2
}

#
# Run one trial.  Returns 0 if the scrub started before the resilver,
# 1 if the resilver won the race.  Fails the test if data is left stale.
#
function trial # <type> <ndevs>
{
	typeset type=$1
	typeset ndevs=$2
	typeset vdevs=""
	typeset i

	rm -rf $VDEVDIR $AWAYDIR
	log_must mkdir -p $VDEVDIR $AWAYDIR
	for i in $(seq 0 $((ndevs - 1))); do
		log_must truncate -s 256m $VDEVDIR/d$i
		vdevs="$vdevs $VDEVDIR/d$i"
	done
	log_must zpool create -f -O compression=off \
	    -O primarycache=metadata -O mountpoint=$TESTDIR2 \
	    $TESTPOOL2 $type $vdevs

	# 1. Data written with every device present.
	log_must file_write -o create -f $TESTDIR2/base -b 1048576 -c 16 \
	    -d R
	sync_pool $TESTPOOL2

	# 2. Take the first device away.
	log_must zpool export $TESTPOOL2
	log_must mv $VDEVDIR/d0 $AWAYDIR/d0
	log_must zpool import -d $VDEVDIR $TESTPOOL2

	# 3. Data the device misses.
	log_must file_write -o create -f $TESTDIR2/away -b 1048576 -c 8 \
	    -d R
	sync_pool $TESTPOOL2

	# 4. Bring the device back and scrub before the resilver starts.
	log_must zpool export $TESTPOOL2
	log_must mv $AWAYDIR/d0 $VDEVDIR/d0
	log_must zpool import -d $VDEVDIR $TESTPOOL2
	zpool scrub -w $TESTPOOL2
	log_must zpool wait -t scrub,resilver $TESTPOOL2

	# 5. The first scan must have been the scrub (func=1).
	typeset first=$(zpool history -i $TESTPOOL2 | \
	    grep -m 1 "scan setup")
	log_note "$type: $first"
	if [[ "$first" != *"func=1"* ]]; then
		destroy_pool $TESTPOOL2
		return 1
	fi

	# The early scrub must have repaired what d0 missed.
	typeset early=$(zpool status $TESTPOOL2 | grep "scan:")
	log_note "$type: early $early"
	if [[ "$early" == *"repaired 0B"* ]]; then
		log_must zpool status -v $TESTPOOL2
		log_fail "$type: the first scrub repaired nothing on d0"
	fi

	# 6. A full scrub must find nothing left to repair.
	log_must zpool clear $TESTPOOL2
	log_must zpool scrub -w $TESTPOOL2
	typeset scan=$(zpool status $TESTPOOL2 | grep "scan:")
	typeset cksum=$(zpool status -p $TESTPOOL2 | \
	    awk -v d="$VDEVDIR/d0" '$1 == d {print $5}')
	log_note "$type: $scan, d0 CKSUM $cksum"
	if [[ "$scan" != *"repaired 0B"* || "$cksum" != "0" ]]; then
		log_must zpool status -v $TESTPOOL2
		log_fail "$type: data missed by d0 was not repaired" \
		    "by the first scrub"
	fi

	destroy_pool $TESTPOOL2
	return 0
}

log_assert "A scrub before the resilver repairs a returned device"
log_onexit cleanup

for layout in "raidz1 3" "raidz2 5" "raidz3 7" \
    "draid1:2d:4c:0s 4"; do
	set -- $layout
	typeset won=false
	for attempt in 1 2 3 4 5; do
		if trial $1 $2; then
			won=true
			break
		fi
		log_note "$1: resilver started first (attempt $attempt)"
	done
	if [[ $won != true ]]; then
		log_unsupported "$1: the resilver always started first"
	fi
done

log_pass "A scrub before the resilver repairs a returned device"
