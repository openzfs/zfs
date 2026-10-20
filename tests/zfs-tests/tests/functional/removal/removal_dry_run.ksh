#! /bin/ksh -p
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
. $STF_SUITE/tests/functional/removal/removal.kshlib

#
# DESCRIPTION:
# "zpool remove -n" fails with the error of a real removal when the pool
# config shows that the removal would be refused, and still prints the
# estimate when it would not.
#
# STRATEGY:
# 1. For each refused case, check that "zpool remove -n" and
#    "zpool remove" both fail with the same message.
# 2. On pools that allow the removal, check that "zpool remove -n" and
#    "zpool remove -np" print the estimate and remove nothing.
#

verify_runnable "global"

DISKDIR=$(mktemp -d)
# Large enough that the kernel's free space check does not fail first.
for i in 1 2 3 4 5; do
	log_must truncate -s 1g $DISKDIR/d$i
done
D1=$DISKDIR/d1
D2=$DISKDIR/d2
D3=$DISKDIR/d3
D4=$DISKDIR/d4
D5=$DISKDIR/d5

function cleanup
{
	set_tunable32 REMOVAL_SUSPEND_PROGRESS 0
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
	log_must rm -rf $DISKDIR
}
log_onexit cleanup

#
# Check that "zpool remove -n" and "zpool remove" both refuse to remove
# the vdev, with the same message, which contains the expected text.
#
function check_refused # pool vdev text
{
	typeset pool=$1
	typeset vdev=$2
	typeset text=$3
	typeset dry
	typeset real

	dry=$(zpool remove -n $pool $vdev 2>&1) && \
	    log_fail "zpool remove -n $pool $vdev succeeded: $dry"
	real=$(zpool remove $pool $vdev 2>&1) && \
	    log_fail "zpool remove $pool $vdev succeeded: $real"
	[[ "$dry" == "$real" ]] || \
	    log_fail "zpool remove -n: '$dry', zpool remove: '$real'"
	[[ "$dry" == *"$text"* ]] || \
	    log_fail "zpool remove -n $pool $vdev: unexpected error '$dry'"
	log_note "$vdev: $dry"
}

#
# Check that "zpool remove -n" and "zpool remove -np" print the estimate
# and leave the vdev in the pool.
#
function check_estimate # pool vdev
{
	typeset pool=$1
	typeset vdev=$2
	typeset out

	out=$(zpool remove -n $pool $vdev) || \
	    log_fail "zpool remove -n $pool $vdev failed: $out"
	[[ "$out" == "Memory that will be used after removing $vdev: "* ]] || \
	    log_fail "zpool remove -n $pool $vdev: unexpected output '$out'"
	out=$(zpool remove -np $pool $vdev) || \
	    log_fail "zpool remove -np $pool $vdev failed: $out"
	echo "$out" | grep -qE "^$vdev [0-9]+\$" || \
	    log_fail "zpool remove -np $pool $vdev: unexpected output '$out'"
	log_must vdevs_in_pool $pool $vdev
	log_note "$vdev: $out"
}

EINVAL_TEXT="invalid config; all top-level vdevs must have the same"
ENOTSUP_TEXT="operation not supported on this type of pool"
EBUSY_TEXT="Pool busy; removal may already be in progress"

log_assert "zpool remove -n refuses what zpool remove would refuse"

# A raidz top-level vdev.
log_must zpool create -f $TESTPOOL $D1 raidz $D2 $D3
check_refused $TESTPOOL $D1 "$EINVAL_TEXT"
log_must destroy_pool $TESTPOOL

# Top-level vdevs with different ashifts.
log_must zpool create -f -o ashift=9 $TESTPOOL $D1
log_must zpool add -f -o ashift=12 $TESTPOOL $D2
check_refused $TESTPOOL $D2 "$EINVAL_TEXT"
log_must destroy_pool $TESTPOOL

#
# A mirror with a non-leaf child (an active spare).  Also check the
# refusals for a mirror leaf and for the spare in use.
#
log_must zpool create -f $TESTPOOL $D3 mirror $D1 $D2 spare $D4
log_must zpool replace -w $TESTPOOL $D1 $D4
check_refused $TESTPOOL $D3 "$EINVAL_TEXT"
check_refused $TESTPOOL $D2 "$ENOTSUP_TEXT"
check_refused $TESTPOOL $D4 "$EBUSY_TEXT"
log_must destroy_pool $TESTPOOL

# A leaf of a log mirror, then the log mirror itself.
log_must zpool create -f $TESTPOOL $D1 log mirror $D2 $D3
check_refused $TESTPOOL $D2 "$ENOTSUP_TEXT"
log_must zpool remove -n $TESTPOOL mirror-1
log_must vdevs_in_pool $TESTPOOL mirror-1
log_must destroy_pool $TESTPOOL

# A checkpoint, which blocks the removal of a cache device too.
log_must zpool create -f $TESTPOOL $D1 $D2 cache $D3
log_must zpool checkpoint $TESTPOOL
check_refused $TESTPOOL $D2 "checkpoint exists"
check_refused $TESTPOOL $D3 "checkpoint exists"
log_must zpool checkpoint -d -w $TESTPOOL
check_estimate $TESTPOOL $D2
log_must destroy_pool $TESTPOOL

# The device_removal feature disabled, then a legacy pool.
log_must zpool create -f -d $TESTPOOL $D1 $D2
check_refused $TESTPOOL $D2 "$ENOTSUP_TEXT"
log_must destroy_pool $TESTPOOL
log_must zpool create -f -o version=28 $TESTPOOL $D1 $D2
check_refused $TESTPOOL $D2 "$ENOTSUP_TEXT"
log_must destroy_pool $TESTPOOL

# A removal in progress, of the same vdev and then of another one.
log_must zpool create -f $TESTPOOL $D1 $D2 $D3
log_must dd if=/dev/urandom of=/$TESTPOOL/file bs=1M count=32
sync_pool $TESTPOOL
log_must set_tunable32 REMOVAL_SUSPEND_PROGRESS 1
log_must zpool remove $TESTPOOL $D1
sync_pool $TESTPOOL
log_must is_pool_removing $TESTPOOL
check_refused $TESTPOOL $D1 "removal for this vdev is already in progress"
check_refused $TESTPOOL $D2 "$EBUSY_TEXT"
log_must set_tunable32 REMOVAL_SUSPEND_PROGRESS 0
log_must wait_for_removal $TESTPOOL
check_estimate $TESTPOOL $D2
log_must destroy_pool $TESTPOOL

# Removals that are allowed: the output is unchanged.
log_must zpool create -f $TESTPOOL mirror $D1 $D2 mirror $D3 $D4 cache $D5
check_estimate $TESTPOOL mirror-1
log_must zpool remove -n $TESTPOOL $D5
log_must vdevs_in_pool $TESTPOOL $D5
log_must destroy_pool $TESTPOOL

log_pass "zpool remove -n refuses what zpool remove would refuse"
