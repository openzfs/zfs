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
# A top-level vdev which has no allocated space is turned into a hole in
# any pool, whatever its type: no indirect vdev is created, and the feature
# references taken when the vdev was added or expanded are given back.
#
# STRATEGY:
# 1. Create a pool without raidz, add an empty disk and remove it.  Verify
#    that no indirect vdev is created and that device_removal does not
#    become active.
# 2. Add an empty dRAID vdev and remove it.  Verify that its distributed
#    spare goes with it and that the draid feature is no longer active,
#    also after an export and import.
# 3. Add an empty dedup raidz vdev, expand it and remove it.  Verify that
#    the raidz_expansion feature is no longer active.
# 4. Verify the pool with zdb and a scrub.
# 5. Create a pool with no feature enabled, add an empty disk and remove it.
#

verify_runnable "global"

DISKDIR=$(mktemp -d)

function cleanup
{
	default_cleanup_noexit
	log_must rm -rf $DISKDIR
}

function has_indirect_vdev
{
	zpool status $TESTPOOL | grep -q "indirect-"
}

# Turn off allocations from a vdev which was just added to keep it empty.
function add_empty_vdev # vdev args...
{
	typeset vdev=$1
	shift

	log_must zpool add -f $TESTPOOL "$@"
	log_must zpool set allocating=off $TESTPOOL $vdev
	log_must test "$(get_vdev_prop allocated $TESTPOOL $vdev)" -eq 0
}

function remove_empty_vdev # vdev
{
	log_must zpool remove $TESTPOOL $1
	log_mustnot vdevs_in_pool $TESTPOOL $1
	log_mustnot has_indirect_vdev
}

log_onexit cleanup

typeset -a disk
typeset -i i
for i in {1..8}; do
	disk[$i]="$DISKDIR/dsk$i"
	log_must mkfile $MINVDEVSIZE ${disk[$i]}
done

#
# 1. A disk in a pool without raidz.
#
log_must default_setup_noexit "mirror ${disk[1]} ${disk[2]}"
log_must file_write -o create -f $TESTDIR/file -b $((2**17)) -c $((2**6)) \
    -d R
log_must sync_pool $TESTPOOL
add_empty_vdev ${disk[3]} ${disk[3]}
remove_empty_vdev ${disk[3]}
log_must test "$(get_pool_prop feature@device_removal $TESTPOOL)" = "enabled"

#
# 2. A dRAID vdev, with its distributed spare.
#
log_must test "$(get_pool_prop feature@draid $TESTPOOL)" = "enabled"
add_empty_vdev draid1:1d:4c:1s-2 draid1:1d:4c:1s ${disk[3]} ${disk[4]} \
    ${disk[5]} ${disk[6]}
log_must test "$(get_pool_prop feature@draid $TESTPOOL)" = "active"
log_must vdevs_in_pool $TESTPOOL draid1-2-0
remove_empty_vdev draid1:1d:4c:1s-2
log_mustnot vdevs_in_pool $TESTPOOL draid1-2-0
log_must test "$(get_pool_prop feature@draid $TESTPOOL)" = "enabled"
log_must zpool export $TESTPOOL
log_must zpool import -d $DISKDIR $TESTPOOL
log_mustnot vdevs_in_pool $TESTPOOL draid1-2-0
log_must test "$(get_pool_prop feature@draid $TESTPOOL)" = "enabled"

#
# 3. An expanded raidz vdev.
#
log_must zpool add -f $TESTPOOL dedup raidz1 ${disk[3]} ${disk[4]} ${disk[5]}
log_must zpool attach $TESTPOOL raidz1-3 ${disk[6]}
log_must zpool wait -t raidz_expand $TESTPOOL
log_must test "$(get_vdev_prop allocated $TESTPOOL raidz1-3)" -eq 0
log_must test "$(get_pool_prop feature@raidz_expansion $TESTPOOL)" = "active"
remove_empty_vdev raidz1-3
log_must test "$(get_pool_prop feature@raidz_expansion $TESTPOOL)" = \
    "enabled"
log_must test "$(get_pool_prop feature@device_removal $TESTPOOL)" = "enabled"

#
# 4. The pool is consistent.
#
log_must zpool export $TESTPOOL
log_must zdb -e -p $DISKDIR -bcc $TESTPOOL
log_must zpool import -d $DISKDIR $TESTPOOL
verify_pool $TESTPOOL

#
# 5. No feature enabled.
#
destroy_pool $TESTPOOL
log_must zpool create -f -d $TESTPOOL ${disk[1]}
add_empty_vdev ${disk[2]} ${disk[2]}
remove_empty_vdev ${disk[2]}
log_must test "$(get_pool_prop feature@device_removal $TESTPOOL)" = \
    "disabled"
verify_pool $TESTPOOL

log_pass "Empty top-level vdevs are turned into holes in any pool."
