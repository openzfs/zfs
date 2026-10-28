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
# A scrub requested on a raidz or dRAID pool before the resilver of a
# returned device has run must not take its place.  Such a scrub would
# be limited to the missing txgs and clear the device's DTL, leaving
# data stale until a later full scrub.  It is refused instead, and the
# resilver repairs the device.
#
# STRATEGY:
# For raidz1, raidz2, raidz3 and draid1:
# 1. Create the pool and write some data.
# 2. Export the pool, move the first device away, import it degraded.
# 3. Write more data while the device is away.
# 4. Export the pool, move the device back, import it and request a
#    scrub at once.
# 5. Verify the first scan was the resilver, and that a requested scrub
#    was either refused as "currently resilvering" or ran after it.
# 6. Run a full scrub and verify it repairs nothing on the device.
# 7. On raidz1, make writes to one device fail while it stays online,
#    so that it has a DTL but no resilver is scheduled.  Verify that a
#    scrub is not refused, that it repairs the failed writes, and that
#    a full scrub then repairs nothing.
#

verify_runnable "global"

TESTDIR2="$TEST_BASE_DIR/zpool_scrub_before_resilver"
VDEVDIR="$TEST_BASE_DIR/zpool_scrub_before_resilver.d"
AWAYDIR="$TEST_BASE_DIR/zpool_scrub_before_resilver.away"

function cleanup
{
	zinject -c all
	poolexists $TESTPOOL2 && destroy_pool $TESTPOOL2
	rm -rf $VDEVDIR $AWAYDIR $TESTDIR2
}

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

	# 4. Bring the device back and request a scrub at once.
	log_must zpool export $TESTPOOL2
	log_must mv $AWAYDIR/d0 $VDEVDIR/d0
	log_must zpool import -d $VDEVDIR $TESTPOOL2
	typeset out
	if out=$(zpool scrub $TESTPOOL2 2>&1); then
		log_note "$type: scrub accepted"
	else
		log_note "$type: scrub refused: $out"
		[[ "$out" == *"currently resilvering"* ]] || \
		    log_fail "$type: unexpected scrub error: $out"
	fi
	log_must zpool wait -t resilver $TESTPOOL2
	log_must zpool wait -t scrub $TESTPOOL2

	# 5. The first scan must have been the resilver (func=2).
	typeset first=$(zpool history -i $TESTPOOL2 | \
	    grep -m 1 "scan setup")
	log_note "$type: $first"
	[[ "$first" == *"func=2"* ]] || \
	    log_fail "$type: a scrub ran before the resilver"

	# 6. A full scrub must find nothing left to repair.
	log_must zpool clear $TESTPOOL2
	log_must zpool scrub -w $TESTPOOL2
	typeset scan=$(zpool status $TESTPOOL2 | grep "scan:")
	typeset cksum=$(zpool status -p $TESTPOOL2 | \
	    awk -v d="$VDEVDIR/d0" '$1 == d {print $5}')
	log_note "$type: $scan, d0 CKSUM $cksum"
	if [[ "$scan" != *"repaired 0B"* || "$cksum" != "0" ]]; then
		log_must zpool status -v $TESTPOOL2
		log_fail "$type: data missed by d0 was not repaired"
	fi

	destroy_pool $TESTPOOL2
}

function trial_write_errors
{
	typeset vdevs=""
	typeset i

	rm -rf $VDEVDIR
	log_must mkdir -p $VDEVDIR
	for i in 0 1 2; do
		log_must truncate -s 256m $VDEVDIR/d$i
		vdevs="$vdevs $VDEVDIR/d$i"
	done
	log_must zpool create -f -O compression=off -O recordsize=16k \
	    -O primarycache=metadata -O mountpoint=$TESTDIR2 \
	    $TESTPOOL2 raidz1 $vdevs

	log_must zinject -d $VDEVDIR/d0 -e io-prefail -T write -f 25 \
	    $TESTPOOL2
	log_must file_write -o create -f $TESTDIR2/werr -b 131072 -c 200 \
	    -d R
	sync_pool $TESTPOOL2
	log_must zinject -c all
	log_must check_pool_device $TESTPOOL2 "$VDEVDIR/d0" "ONLINE"

	# No resilver is scheduled for the failed writes, so the scrub
	# must not be refused, and it must repair them.
	log_must zpool scrub -w $TESTPOOL2
	typeset scan=$(zpool status $TESTPOOL2 | grep "scan:")
	log_note "write errors: $scan"
	if [[ "$scan" == *"repaired 0B"* ]]; then
		log_must zpool status -v $TESTPOOL2
		log_fail "write errors: the scrub repaired nothing"
	fi

	log_must zpool clear $TESTPOOL2
	log_must zpool scrub -w $TESTPOOL2
	scan=$(zpool status $TESTPOOL2 | grep "scan:")
	log_note "write errors: $scan"
	if [[ "$scan" != *"repaired 0B"* ]]; then
		log_must zpool status -v $TESTPOOL2
		log_fail "write errors: the first scrub left data to repair"
	fi

	destroy_pool $TESTPOOL2
}

log_assert "A scrub requested before the resilver does not replace it"
log_onexit cleanup

for layout in "raidz1 3" "raidz2 5" "raidz3 7" \
    "draid1:2d:4c:0s 4"; do
	set -- $layout
	trial $1 $2
done
trial_write_errors

log_pass "A scrub requested before the resilver does not replace it"
