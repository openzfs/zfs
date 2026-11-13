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

#
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# A top-level vdev which has no allocated space can be removed from a
# pool with a raidz top-level vdev, whatever its type.  Nothing is copied,
# the vdev is replaced by a hole and no indirect vdev is created.
#
# STRATEGY:
# 1. Create a pool with a raidz1 top-level vdev and write some data.
# 2. For each of a raidz1, raidz2, raidz3, disk, mirror, special mirror and
#    dedup top-level vdev:
#    a. Add the vdev and turn off allocations from it right away.
#    b. Write more data and verify that the new vdev stays empty.
#    c. Remove the vdev and verify that it is gone at once, that no
#       indirect vdev exists, that device_removal did not become active
#       and that the pool is back to its original size.
# 3. Export the pool, verify it with zdb, import it and scrub it.
# 4. Verify that a vdev can still be added to the pool and removed again.
#

verify_runnable "global"

DISKDIR=$(mktemp -d)

function cleanup
{
	default_cleanup_noexit
	log_must restore_tunable TXG_TIMEOUT
	log_must rm -rf $DISKDIR
}

function has_indirect_vdev
{
	zpool status $TESTPOOL | grep -q "indirect-"
}

typeset -i nremoved=0
typeset -i size_orig

#
# Add a top-level vdev and remove it while it is empty.
#
# $1 - the name of the vdev in the pool
# $2.. - the arguments of the "zpool add" command
#
function remove_empty_vdev
{
	typeset vdev=$1
	shift

	log_note "Removing the empty vdev '$vdev'"

	log_must zpool add -f $TESTPOOL "$@"
	log_must zpool set allocating=off $TESTPOOL $vdev
	log_must test "$(get_pool_prop size $TESTPOOL)" -gt $size_orig

	# The data goes to the first vdev only.
	log_must file_write -o create -f $TESTDIR/file.$nremoved \
	    -b $((2**17)) -c $((2**6)) -d R
	log_must sync_pool $TESTPOOL
	log_must test "$(get_vdev_prop allocated $TESTPOOL $vdev)" -eq 0

	log_must zpool remove $TESTPOOL $vdev
	nremoved+=1

	log_mustnot vdevs_in_pool $TESTPOOL $vdev
	log_mustnot has_indirect_vdev
	log_must test "$(get_pool_prop feature@device_removal $TESTPOOL)" = \
	    "enabled"
	log_must test "$(get_pool_prop size $TESTPOOL)" -eq $size_orig
}

log_must save_tunable TXG_TIMEOUT
log_onexit cleanup

typeset -a disk
typeset -i i
for i in {1..20}; do
	disk[$i]="$DISKDIR/dsk$i"
	log_must mkfile $MINVDEVSIZE ${disk[$i]}
done

# Keep the pool quiet, a txg written between "zpool add" and "zpool set
# allocating=off" could put metadata on the new vdev.
log_must set_tunable32 TXG_TIMEOUT 5000

log_must default_setup_noexit "raidz1 ${disk[1]} ${disk[2]} ${disk[3]}"
log_must file_write -o create -f $TESTDIR/file -b $((2**17)) -c $((2**6)) \
    -d R
log_must sync_pool $TESTPOOL
size_orig=$(get_pool_prop size $TESTPOOL)

# The vdev ids go up with every vdev added, the ids of the holes are not reused.
remove_empty_vdev raidz1-1 raidz1 ${disk[4]} ${disk[5]} ${disk[6]}
remove_empty_vdev raidz2-2 raidz2 ${disk[7]} ${disk[8]} ${disk[9]}
remove_empty_vdev raidz3-3 raidz3 ${disk[10]} ${disk[11]} ${disk[12]} \
    ${disk[13]}
remove_empty_vdev ${disk[14]} ${disk[14]}
remove_empty_vdev mirror-5 mirror ${disk[15]} ${disk[16]}
remove_empty_vdev mirror-6 special mirror ${disk[17]} ${disk[18]}
remove_empty_vdev ${disk[19]} dedup ${disk[19]}

log_must test "$(zpool history -i $TESTPOOL | \
    grep -c "\[txg:[0-9]*\] vdev remove $TESTPOOL vdev (")" -eq $nremoved

log_must zpool export $TESTPOOL
log_must zdb -e -p $DISKDIR -bcc $TESTPOOL
log_must zpool import -d $DISKDIR $TESTPOOL
verify_pool $TESTPOOL

remove_empty_vdev ${disk[20]} ${disk[20]}
verify_pool $TESTPOOL

log_pass "Empty top-level vdevs are removed from a pool with a raidz vdev."
