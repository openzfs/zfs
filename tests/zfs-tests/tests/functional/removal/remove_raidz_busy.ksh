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
# The removal of a top-level vdev from a pool with a raidz top-level vdev
# is still refused unless the vdev has no allocated space, and a vdev with
# no allocated space is not removed while a checkpoint exists, while a raidz
# is being expanded, or while a distributed spare of a dRAID vdev is in use.
#
# STRATEGY:
# 1. Add a raidz vdev, let it receive data and try to remove it.  Verify
#    that the removal fails as it always did and leaves the vdev alone.
# 2. Add an empty raidz vdev to a pool with a checkpoint.  Verify that it
#    cannot be removed until the checkpoint is discarded.
# 3. Add an empty raidz vdev to a pool and start a raidz expansion of the
#    first vdev.  Verify that the empty vdev cannot be removed until the
#    expansion is finished.
# 4. Add an empty dRAID vdev to a dRAID pool and replace one of its disks
#    with its distributed spare.  Verify that it cannot be removed until
#    the spare is detached, and that only its own spare goes with it, also
#    after an export and import.
#

verify_runnable "global"

DISKDIR=$(mktemp -d)

function cleanup
{
	default_cleanup_noexit
	log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
	log_must restore_tunable TXG_TIMEOUT
	log_must rm -rf $DISKDIR
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

function write_data # name
{
	log_must file_write -o create -f $TESTDIR/$1 \
	    -b $((2**17)) -c $((2**8)) -d R
	log_must sync_pool $TESTPOOL
}

log_must save_tunable TXG_TIMEOUT
log_onexit cleanup

typeset -a disk
typeset -i i
for i in {1..12}; do
	disk[$i]="$DISKDIR/dsk$i"
	log_must mkfile $MINVDEVSIZE ${disk[$i]}
done

# Keep the pool quiet, a txg written between "zpool add" and "zpool set
# allocating=off" could put metadata on the new vdev.
log_must set_tunable32 TXG_TIMEOUT 5000

#
# 1. A vdev which holds data.
#
log_must default_setup_noexit "raidz1 ${disk[1]} ${disk[2]} ${disk[3]}"
log_must zpool add -f $TESTPOOL raidz1 ${disk[4]} ${disk[5]} ${disk[6]}
write_data file1
log_must test "$(get_vdev_prop allocated $TESTPOOL raidz1-1)" -gt 0
log_mustnot_expect "invalid config" zpool remove $TESTPOOL raidz1-1
log_must vdevs_in_pool $TESTPOOL raidz1-1
log_must test "$(get_vdev_prop allocating $TESTPOOL raidz1-1)" = "on"
write_data file2
verify_pool $TESTPOOL

#
# 2. A checkpoint.
#
destroy_pool $TESTPOOL
log_must default_setup_noexit "raidz1 ${disk[1]} ${disk[2]} ${disk[3]}"
write_data file1
add_empty_vdev raidz1-1 raidz1 ${disk[4]} ${disk[5]} ${disk[6]}
log_must zpool checkpoint $TESTPOOL
log_mustnot_expect "checkpoint exists" zpool remove $TESTPOOL raidz1-1
log_must vdevs_in_pool $TESTPOOL raidz1-1
log_must zpool checkpoint -d $TESTPOOL
log_must zpool wait -t discard $TESTPOOL
log_must zpool remove $TESTPOOL raidz1-1
log_mustnot vdevs_in_pool $TESTPOOL raidz1-1

#
# 3. A raidz expansion.
#
destroy_pool $TESTPOOL
log_must default_setup_noexit "raidz1 ${disk[1]} ${disk[2]} ${disk[3]}"
write_data file1
add_empty_vdev raidz1-1 raidz1 ${disk[4]} ${disk[5]} ${disk[6]}
typeset -i allocated
allocated=$(get_pool_prop allocated $TESTPOOL)
log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES $((allocated / 4))
log_must zpool attach $TESTPOOL raidz1-0 ${disk[7]}
log_must wait_raidz_expand_paused $TESTPOOL
log_mustnot_expect "expansion in progress" zpool remove $TESTPOOL raidz1-1
log_must vdevs_in_pool $TESTPOOL raidz1-1
log_must set_tunable64 RAIDZ_EXPAND_MAX_REFLOW_BYTES 0
log_must zpool wait -t raidz_expand $TESTPOOL
log_must zpool remove $TESTPOOL raidz1-1
log_mustnot vdevs_in_pool $TESTPOOL raidz1-1
verify_pool $TESTPOOL

#
# 4. dRAID with its distributed spare in use.
#
destroy_pool $TESTPOOL
log_must zpool create -f $TESTPOOL draid1:1d:4c:1s ${disk[1]} ${disk[2]} \
    ${disk[3]} ${disk[4]}
add_empty_vdev draid1:1d:4c:1s-1 draid1:1d:4c:1s ${disk[5]} ${disk[6]} \
    ${disk[7]} ${disk[8]}
log_must zpool replace $TESTPOOL ${disk[5]} draid1-1-0
log_must zpool wait $TESTPOOL
log_mustnot_expect "busy" zpool remove $TESTPOOL draid1:1d:4c:1s-1
log_must vdevs_in_pool $TESTPOOL draid1:1d:4c:1s-1
log_must zpool detach $TESTPOOL draid1-1-0
log_must zpool remove $TESTPOOL draid1:1d:4c:1s-1
log_mustnot vdevs_in_pool $TESTPOOL draid1:1d:4c:1s-1
log_mustnot vdevs_in_pool $TESTPOOL draid1-1-0
log_must vdevs_in_pool $TESTPOOL draid1-0-0
log_must test "$(get_pool_prop feature@draid $TESTPOOL)" = "active"
log_must zpool export $TESTPOOL
log_must zpool import -d $DISKDIR $TESTPOOL
log_mustnot vdevs_in_pool $TESTPOOL draid1-1-0
log_must vdevs_in_pool $TESTPOOL draid1-0-0
verify_pool $TESTPOOL

log_pass "Only the vdevs which hold no data are removed."
