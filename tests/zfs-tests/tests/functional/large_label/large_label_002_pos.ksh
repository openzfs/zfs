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
# Copyright (c) 2025 by Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/large_label/large_label.kshlib


#
# DESCRIPTION:
# Verify that new label works for more advanced pool operations.
#
# STRATEGY:
#	1. Create large virtual disks for the new label type
#	2. Create pool using large-label disks
#	3. Verify disks are using the new label format
#	4. Perform more advanced pool operations (import, export, scrub, destroy)
#

function cleanup {
	log_pos zpool destroy $TESTPOOL
	log_must rm $mntpnt/dsk*
}

log_assert "Verify that new label works for more advanced pool operations"
log_onexit cleanup

mntpnt="$TESTDIR1"
log_must truncate -s 2T $mntpnt/dsk{0,1,2,3,4,5,6,7}

DSK="$mntpnt/dsk"

log_must create_pool -f $TESTPOOL "$DSK"0 mirror "$DSK"1 "$DSK"2 raidz1 "$DSK"3 "$DSK"4 "$DSK"5 log "$DSK"6 special "$DSK"7

log_must dd if=/dev/urandom of=/$TESTPOOL/f1 bs=1M count=1k
log_must sync_pool $TESTPOOL

for i in `seq 0 7`; do
	log_must zdb -l "$DSK""$i"
	log_must uses_large_label "$DSK""$i"
done

log_must zpool scrub -w $TESTPOOL
log_must zpool export $TESTPOOL
log_must zpool import -d $mntpnt $TESTPOOL

log_pass "New label works for more advanced pool operations"
