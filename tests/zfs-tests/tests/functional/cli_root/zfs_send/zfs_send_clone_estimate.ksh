#!/bin/ksh
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
# Copyright (c) 2026 Juanmi Taboada
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	'zfs send -nvP' estimates the first snapshot of a clone, which -I
#	and -R send as an incremental from the clone's origin, from that
#	origin and not as a full stream.
#
# STRATEGY:
#	1. Create a filesystem with 64M of data and snapshot it
#	2. Clone the snapshot, write 1M to the clone and snapshot it, twice
#	3. For -I from the origin and for -R, compare the estimate with the
#	   size of the stream actually sent
#	4. Verify the first snapshot is reported as an incremental from the
#	   origin
#

verify_runnable "both"

function cleanup
{
	datasetexists $TESTPOOL/$TESTFS1 && \
	    destroy_dataset $TESTPOOL/$TESTFS1 "-R"
}

log_assert "Verify 'zfs send -nvP' estimates a clone from its origin"
log_onexit cleanup

typeset origin="$TESTPOOL/$TESTFS1/fs@origin"
typeset clone="$TESTPOOL/$TESTFS1/clone"

log_must zfs create -p $TESTPOOL/$TESTFS1/fs
typeset mntpnt=$(get_prop mountpoint $TESTPOOL/$TESTFS1/fs)
log_must dd if=/dev/urandom of=$mntpnt/file bs=1M count=64
log_must zfs snapshot $origin
log_must zfs clone $origin $clone
mntpnt=$(get_prop mountpoint $clone)
log_must dd if=/dev/urandom of=$mntpnt/file1 bs=1M count=1
log_must zfs snapshot $clone@s1
log_must dd if=/dev/urandom of=$mntpnt/file2 bs=1M count=1
log_must zfs snapshot $clone@s2
sync_pool $TESTPOOL

for opts in "-I $origin $clone@s2" "-R $clone@s2"; do
	typeset estimate=$(zfs send -nvP $opts | awk '$1 == "size" {print $2}')
	typeset actual=$(zfs send $opts | wc -c)
	typeset first=$(zfs send -nvP $opts | awk 'NR == 1 {print $1, $2, $3}')

	log_note "zfs send $opts: estimate $estimate, sent $actual"
	[[ "$first" == "incremental $origin $clone@s1" ]] || \
	    log_fail "zfs send -nvP $opts: first line '$first'"
	(( estimate * 10 >= actual * 9 && estimate * 10 <= actual * 11 )) || \
	    log_fail "zfs send -nvP $opts: estimate $estimate, sent $actual"
done

log_pass "'zfs send -nvP' estimates a clone from its origin"
