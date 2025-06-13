#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
#
# CDDL HEADER START
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
# CDDL HEADER END
#

#
# Copyright (c) 2025 by George Amanakis. All rights reserved.
#

. $STF_SUITE/tests/functional/rsend/rsend.kshlib

#
# DESCRIPTION:
# Verify that an incremental non-raw zfs send from an encrypted filesystem
# does not leak any keys or key mappings.
#
# STRATEGY:
# 1. Create a new encrypted filesystem
# 2. Write some files and create snapshots.
# 3. Send to a new filesystem
# 4. Do an incremental (-I) send and before that access all properties on the
#	sending filesystem (emulate sanoid)
# 5. Export and re-import the pool. Upon exporting the pool if any keys/key
#	mappings leaked a panic will occur.
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS2 && \
		destroy_dataset $TESTPOOL/$TESTFS2 -r
	datasetexists $TESTPOOL/recv && \
		destroy_dataset $TESTPOOL/recv -r
	[[ -f $keyfile ]] && log_must rm $keyfile
}
log_onexit cleanup

log_assert "Verify non-raw send with encryption does not leak any key mappings"

typeset keyfile=/$TESTPOOL/pkey

# Create an encrypted dataset
log_must eval "echo 'password' > $keyfile"
log_must zfs create -o encryption=on -o keyformat=passphrase \
	-o keylocation=file://$keyfile $TESTPOOL/$TESTFS2

log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS2/testfile bs=128K count=4 \
	status=none

for i in $(seq 0 20); do
    log_note "Taking snapshots"
    log_must zfs snapshot $TESTPOOL/$TESTFS2@snap_$i
    log_must dd if=/dev/urandom of=/$TESTPOOL/$TESTFS2/testfile bs=128K \
	    count=4 status=none
done

log_must eval "zfs send $TESTPOOL/$TESTFS2@snap_0 | zfs recv $TESTPOOL/recv"

for i in $(seq 3 3 20); do
    log_note "Sending incremental snapshot snap_$((i - 3)) -> snap_$i"
    log_must zfs get -Hpd 1 -t snapshot all $TESTPOOL/$TESTFS2 &>/dev/null
    log_must eval "zfs send -I $TESTPOOL/$TESTFS2@snap_$((i - 3)) \
	$TESTPOOL/$TESTFS2@snap_$i | zfs recv $TESTPOOL/recv"
done

log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL

log_pass "Verify non-raw send with encryption does not leak any key mappings"
