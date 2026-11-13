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
#	"zpool set allocating=off" fails when the pool could not hold its
#	data without the vdev, and then leaves the vdev allocating, in
#	memory and in the pool config.
#
# STRATEGY:
#	1. Create a pool of two file vdevs and fill it with more than one
#	   of them can hold.
#	2. Set allocating=off on the first vdev: the command fails, the vdev
#	   is not shown as non-allocating, the available space is the same,
#	   and the vdev still allocates after an export and import.
#	3. Free the space: allocating=off now succeeds.
#	4. Set allocating=on again.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -f $VDEV0 $VDEV1
}

function check_allocating # on|off
{
	typeset value=$(zpool get -H -o value allocating $TESTPOOL1 $VDEV0)
	[[ "$value" == "$1" ]] || \
	    log_fail "zpool get allocating is '$value', expected '$1'"

	typeset shown=on
	zpool status $TESTPOOL1 | grep -q "non-allocating" && shown=off
	[[ "$shown" == "$1" ]] || \
	    log_fail "zpool status shows allocating $shown, expected $1"
}

log_assert "A refused allocating=off leaves the vdev allocating"
log_onexit cleanup

VDEV0=$TESTDIR/vdev_alloc_enospc0
VDEV1=$TESTDIR/vdev_alloc_enospc1
log_must truncate -s $MINVDEVSIZE $VDEV0 $VDEV1
log_must zpool create -O compression=off $TESTPOOL1 $VDEV0 $VDEV1
log_must zfs create $TESTPOOL1/fill

# 80% of the space the pool offers is more than one of its vdevs holds.
typeset -i space=$(( $(get_prop used $TESTPOOL1) + \
    $(get_prop available $TESTPOOL1) ))
typeset -i mb=$(( space * 8 / 10 / 1048576 ))
typeset mntpnt=$(get_prop mountpoint $TESTPOOL1/fill)
log_must dd if=/dev/urandom of=$mntpnt/file bs=1048576 count=$mb
sync_pool $TESTPOOL1

typeset -i avail=$(get_prop available $TESTPOOL1)
log_mustnot zpool set allocating=off $TESTPOOL1 $VDEV0
check_allocating on
sync_pool $TESTPOOL1
typeset -i avail_after=$(get_prop available $TESTPOOL1)
(( avail_after * 100 >= avail * 95 )) || \
    log_fail "available went from $avail to $avail_after"

log_must zpool export $TESTPOOL1
log_must zpool import -d $TESTDIR $TESTPOOL1
check_allocating on

log_must zfs destroy $TESTPOOL1/fill
wait_freeing $TESTPOOL1
sync_pool $TESTPOOL1
log_must zpool set allocating=off $TESTPOOL1 $VDEV0
check_allocating off
log_must zpool set allocating=on $TESTPOOL1 $VDEV0
check_allocating on

log_pass "A refused allocating=off leaves the vdev allocating"
