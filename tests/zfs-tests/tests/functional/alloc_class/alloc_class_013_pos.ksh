#!/bin/ksh -p

#
# This file and its contents are supplied under the terms of the
# Common Development and Distribution License ("CDDL"), version 1.0.
# You may only use this file in accordance with the terms of version
# 1.0 of the CDDL.
#
# A full copy of the text of the CDDL should have accompanied this
# source.  A copy of the CDDL is also available via the Internet at
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2018 by Delphix. All rights reserved.
#

. $STF_SUITE/tests/functional/alloc_class/alloc_class.kshlib

#
# DESCRIPTION:
#	Removing a dedup device from a pool succeeds.
#

verify_runnable "global"

claim="Removing a dedup device from a pool succeeds."

log_assert $claim
log_onexit cleanup

#
# Create a non-raidz pool so we can remove top-level vdevs
#
log_must disk_setup
log_must zpool create $TESTPOOL $ZPOOL_DISKS dedup $CLASS_DISK0
log_must display_status "$TESTPOOL"

#
# Generate some dedup data in the dedup class before removal
#

log_must zfs create -o dedup=on -V 2G $TESTPOOL/$TESTVOL

log_must eval "new_fs $ZVOL_DEVDIR/$TESTPOOL/$TESTVOL >/dev/null 2>&1"

sync_pool
log_must zpool list -v $TESTPOOL

#
# remove a dedup allocation vdev
#
log_must zpool remove $TESTPOOL $CLASS_DISK0

sleep 5
sync_pool $TESTPOOL
sleep 1

log_must zdb -bbcc $TESTPOOL

log_must zpool destroy -f "$TESTPOOL"

log_pass $claim
