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
# Copyright (c) 2017, Datto, Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# ZFS should perform raw sends of datasets.
#
# STRATEGY:
# 1. Create an encrypted dataset
# 2. Snapshot the default dataset and the encrypted dataset
# 3. Attempt a raw send of both datasets
# 4. Attempt a raw send with properties of both datasets
# 5. Attempt a raw replication send of both datasets
# 6. Unmount the encrypted dataset and attempt a raw send of the file
#    system itself, with zfs send and with zdb -B
# 7. Unload the encrypted dataset key
# 8. Attempt a raw send of the encrypted dataset
#

verify_runnable "both"

function cleanup
{
	snapexists $snap && destroy_dataset $snap
	datasetexists $TESTPOOL/$TESTFS1 && \
		destroy_dataset $TESTPOOL/$TESTFS1 -r
}

log_onexit cleanup

log_assert "ZFS should perform raw sends of datasets"

typeset passphrase="password"
typeset snap="$TESTPOOL/$TESTFS@snap"
typeset snap1="$TESTPOOL/$TESTFS1@snap"

log_must eval "echo $passphrase | zfs create -o encryption=on" \
	"-o keyformat=passphrase $TESTPOOL/$TESTFS1"

log_must zfs snapshot $snap
log_must zfs snapshot $snap1

log_must eval "zfs send -w $snap > /dev/null"
log_must eval "zfs send -w $snap1 > /dev/null"

log_note "Verify ZFS can perform raw sends with properties"
log_must eval "zfs send -wp $snap > /dev/null"
log_must eval "zfs send -wp $snap1 > /dev/null"

log_note "Verify ZFS can perform raw replication sends"
log_must eval "zfs send -wR $snap > /dev/null"
log_must eval "zfs send -wR $snap1 > /dev/null"

log_note "Verify ZFS can perform a raw send of an unmounted encrypted" \
	"file system, not just of its snapshots"
log_must zfs unmount $TESTPOOL/$TESTFS1
log_must eval "zfs send -w $TESTPOOL/$TESTFS1 > /dev/null"
typeset objsetid=$(get_prop objsetid $TESTPOOL/$TESTFS1)
log_must eval "zdb -B $TESTPOOL/$objsetid w > /dev/null"

log_note "Verify ZFS can perform a raw send of an encrypted datasets with" \
	"its key unloaded"
log_must zfs unload-key $TESTPOOL/$TESTFS1
log_must eval "zfs send -w $snap1 > /dev/null"

log_pass "ZFS performs raw sends of datasets"
