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
# Copyright (c) 2024, Klara Inc.
#

. $STF_SUITE/include/libtest.shlib

DATAFILE=$(mktemp)

function cleanup
{
	zpool clear $TESTPOOL
	destroy_pool $TESTPOOL
	unload_scsi_debug
	rm -f $DATAFILE
}

log_onexit cleanup

log_assert "ensure single-disk pool resumes properly after suspend and clear"

# create a file, and take a checksum, so we can compare later
log_must dd if=/dev/urandom of=$DATAFILE bs=128K count=1
typeset sum1=$(xxh128digest $DATAFILE)

# make a debug device that we can "unplug"
load_scsi_debug 100 1 1 1 '512b'
sd=$(get_debug_device)

# create a single-device pool
log_must zpool create $TESTPOOL $sd
log_must zpool sync

# "pull" the disk
log_must eval "echo offline > /sys/block/$sd/device/state"

# copy data onto the pool. it'll appear to succeed, but only be in memory
log_must cp $DATAFILE /$TESTPOOL/file

# wait until sync starts, and the pool suspends
log_note "waiting for pool to suspend"
typeset -i tries=10
until [[ $(kstat_pool $TESTPOOL state) == "SUSPENDED" ]] ; do
	if ((tries-- == 0)); then
		log_fail "pool didn't suspend"
	fi
	sleep 1
done

# return the disk
log_must eval "echo running > /sys/block/$sd/device/state"

# clear the error states, which should reopen the vdev, get the pool back
# online, and replay the failed IO
log_must zpool clear $TESTPOOL

# wait a while for everything to sync out. if something is going to go wrong,
# this is where it will happen
log_note "giving pool time to settle and complete txg"
sleep 7

# if the pool suspended, then everything is bad
if [[ $(kstat_pool $TESTPOOL state) == "SUSPENDED" ]] ; then
	log_fail "pool suspended"
fi

# export the pool, to make sure it exports clean, and also to clear the file
# out of the cache
log_must zpool export $TESTPOOL

# import the pool
log_must zpool import $TESTPOOL

# sum the file we wrote earlier
typeset sum2=$(xxh128digest /$TESTPOOL/file)

# make sure the checksums match
log_must test "$sum1" = "$sum2"

log_pass "single-disk pool resumes properly after disk suspend and clear"
