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
# A top-level vdev which holds data cannot be removed while its data cannot
# be evacuated, and zpool says why.
#
# STRATEGY:
# 1. Add a mirror to a pool with an AnyRAID vdev, let it receive data and
#    try to remove it.
# 2. Replace a disk of a mirror with a hot spare, then try to remove another
#    mirror which holds data.  Verify that it can be removed once the spare
#    is detached.
# 3. Add a special vdev with another ashift, let it receive metadata and try
#    to remove it.  Verify that a normal vdev can still be removed.
# 4. Add a normal vdev with another ashift and try to remove the first one.
# In each case, verify that the removal fails with the message which names
# that cause, and that the vdev stays and still allocates.
#

verify_runnable "global"

DISKDIR=$(mktemp -d)

function cleanup
{
	default_cleanup_noexit
	log_must rm -rf $DISKDIR
	restore_tunable ANYRAID_MIN_TILE_SIZE
}

function write_data # name
{
	log_must file_write -o create -f $TESTDIR/$1 \
	    -b $((2**17)) -c $((2**8)) -d R
	log_must sync_pool $TESTPOOL
}

function remove_not_empty # vdev cause
{
	typeset vdev=$1
	typeset cause=$2

	log_must test "$(get_vdev_prop allocated $TESTPOOL $vdev)" -gt 0
	log_mustnot_expect "cannot be evacuated while $cause" \
	    zpool remove $TESTPOOL $vdev
	log_must vdevs_in_pool $TESTPOOL $vdev
	log_must test "$(get_vdev_prop allocating $TESTPOOL $vdev)" = "on"
}

log_onexit cleanup

# Let AnyRAID use 64M tiles, so that 512M files can be its children.
log_must save_tunable ANYRAID_MIN_TILE_SIZE
log_must set_tunable64 ANYRAID_MIN_TILE_SIZE $((64 * 1024 * 1024))

typeset -a disk
typeset -a adisk
typeset -i i
for i in {1..6}; do
	disk[$i]="$DISKDIR/dsk$i"
	log_must mkfile $MINVDEVSIZE ${disk[$i]}
done
for i in {1..3}; do
	adisk[$i]="$DISKDIR/adsk$i"
	log_must truncate -s 512M ${adisk[$i]}
done

#
# 1. An AnyRAID vdev.
#
log_must default_setup_noexit "anymirror1 ${adisk[1]} ${adisk[2]} ${adisk[3]}"
log_must zpool add -f $TESTPOOL mirror ${disk[1]} ${disk[2]}
write_data file1
remove_not_empty mirror-1 "a top-level vdev has parity"

#
# 2. A hot spare in use.
#
destroy_pool $TESTPOOL
log_must default_setup_noexit "mirror ${disk[1]} ${disk[2]} \
    mirror ${disk[3]} ${disk[4]} spare ${disk[5]}"
write_data file1
log_must zpool replace $TESTPOOL ${disk[2]} ${disk[5]}
log_must zpool wait -t resilver $TESTPOOL
log_must check_hotspare_state $TESTPOOL ${disk[5]} "INUSE"
remove_not_empty mirror-1 "a mirror has a device being replaced or a hot spare"
log_must zpool detach $TESTPOOL ${disk[5]}
log_must zpool remove -w $TESTPOOL mirror-1
log_mustnot vdevs_in_pool $TESTPOOL mirror-1
verify_pool $TESTPOOL

#
# 3. A special vdev with another ashift.
#
destroy_pool $TESTPOOL
log_must default_setup_noexit "mirror ${disk[1]} ${disk[2]} \
    mirror ${disk[3]} ${disk[4]}"
typeset -i ashift
ashift=$(get_vdev_prop ashift $TESTPOOL mirror-0)
log_must zpool add -f -o ashift=$((ashift + 1)) $TESTPOOL \
    special mirror ${disk[5]} ${disk[6]}
write_data file1
remove_not_empty mirror-2 \
    "the normal and special vdevs do not all have the same sector size"
log_must zpool remove -w $TESTPOOL mirror-1
log_mustnot vdevs_in_pool $TESTPOOL mirror-1
verify_pool $TESTPOOL

#
# 4. A normal vdev with another ashift.
#
destroy_pool $TESTPOOL
log_must default_setup_noexit "mirror ${disk[1]} ${disk[2]}"
log_must zpool add -f -o ashift=$((ashift + 1)) $TESTPOOL \
    mirror ${disk[3]} ${disk[4]}
write_data file1
remove_not_empty mirror-0 \
    "the normal vdevs do not all have the same sector size"

log_pass "A vdev which holds data and cannot be evacuated is not removed."
