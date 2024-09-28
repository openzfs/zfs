#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or https://opensource.org/licenses/CDDL-1.0.
# See the License for the specific language governing permissions
# and limitations under the License.
#
# When distributing Covered Code, include this CDDL HEADER in each
# file and include the License file at usr/src/OPENSOLARIS.LICENSE.
# If applicable, add the following below this CDDL HEADER, with the
# fields enclosed by brackets "[]" replaced with your own identifying
# information: Portions Copyright [yyyy] [name of copyright owner]
#
# CDDL HEADER END
#

#
# Copyright (c) 2024, Klara Inc.
#

. $STF_SUITE/include/libtest.shlib

set -x

DATAFILE="$TMPDIR/datafile"

function cleanup
{
	destroy_pool $TESTPOOL
	unload_scsi_debug
	rm -f $DATA_FILE
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
until [[ $(cat /proc/spl/kstat/zfs/$TESTPOOL/state) == "SUSPENDED" ]] ; do
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
if [[ $(cat /proc/spl/kstat/zfs/$TESTPOOL/state) == "SUSPENDED" ]] ; then
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
