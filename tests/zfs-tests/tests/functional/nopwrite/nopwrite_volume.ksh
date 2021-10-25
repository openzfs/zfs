#!/bin/ksh

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
# Copyright (c) 2012, 2016 by Delphix. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/nopwrite/nopwrite.shlib

#
# Description:
# Verify that nopwrite cannot be enabled on volumes
#
# Strategy:
# 1. Create a clone of a volume that fits the criteria for nopwrite.
# 2. Overwrite the same blocks from the origin vol and verify that
# new space is consumed.
#

verify_runnable "global"
origin="$TESTPOOL/$TESTVOL"
clone="$TESTPOOL/clone"
vol="${ZVOL_RDEVDIR}/$origin"
volclone="${ZVOL_RDEVDIR}/$clone"
log_onexit cleanup

function cleanup
{
	datasetexists $origin && destroy_dataset $origin -R
	# No need to recreate the volume as no other tests expect it.
}

log_assert "nopwrite works on volumes"

log_must zfs set compress=on $origin
log_must zfs set checksum=sha256 $origin
dd if=/dev/urandom of=$vol bs=8192 count=4096 conv=notrunc >/dev/null \
    2>&1 || log_fail "dd into $origin failed."
zfs snapshot $origin@a || log_fail "zfs snap failed"
log_must zfs clone $origin@a $clone
log_must zfs set compress=on $clone
log_must zfs set checksum=sha256 $clone
block_device_wait
dd if=$vol of=$volclone bs=8192 count=4096 conv=notrunc >/dev/null 2>&1 || \
    log_fail "dd into $clone failed."
log_must verify_nopwrite $origin $origin@a $clone

log_pass "nopwrite works on volumes"
