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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2012, 2016, Delphix. All rights reserved.
# Copyright (c) 2019, Kjeld Schouten-Lebbing. All Rights Reserved.
#

. $STF_SUITE/include/properties.shlib
. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/nopwrite/nopwrite.shlib

#
# Description:
# Verify that if the checksum on the origin and clone is sha256, any compression
# algorithm enables nopwrite.
#
# Strategy:
# 1. Create an origin dataset with compression and sha256 checksum.
# 2. Write a 64M file into the origin dataset.
# 3. For each of 4 randomly chosen compression types:
# 3a. Create a snap and clone (inheriting the checksum property) of the origin.
# 3b. Apply the compression property to the clone.
# 3c. Write the same 64M of data into the file that exists in the clone.
# 3d. Verify that no new space was consumed.
#

verify_runnable "global"
origin="$TESTPOOL/$TESTFS"
log_onexit cleanup

function cleanup
{
	datasetexists $origin && destroy_dataset $origin -R
	log_must zfs create -o mountpoint=$TESTDIR $origin
}

log_assert "nopwrite works with sha256 and any compression algorithm"

log_must zfs set compress=on $origin
log_must zfs set checksum=sha256 $origin
dd if=/dev/urandom of=$TESTDIR/file bs=1024k count=$MEGS conv=notrunc \
    >/dev/null 2>&1 || log_fail "initial dd failed."

# Verify nop_write for all compression algorithms except "off"
for i in "${compress_prop_vals[@]:1}"; do
	zfs snapshot $origin@a || log_fail "zfs snap failed"
	log_must zfs clone -o compress=$i $origin@a $origin/clone
	dd if=/$TESTDIR/file of=/$TESTDIR/clone/file bs=1024k count=$MEGS \
	    conv=notrunc >/dev/null 2>&1 || log_fail "dd failed."
	log_must verify_nopwrite $origin $origin@a $origin/clone
	zfs destroy -R $origin@a || log_fail "zfs destroy failed"
done

log_pass "nopwrite works with sha256 and any compression algorithm"
