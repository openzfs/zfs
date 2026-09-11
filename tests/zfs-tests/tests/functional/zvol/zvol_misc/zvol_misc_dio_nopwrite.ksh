#!/bin/ksh -p
# SPDX-License-Identifier: CDDL-1.0
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
# Copyright 2026, tiehexue <tiehexue@hotmail.com>. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib

#
# DESCRIPTION:
#	Verify that zvol Direct I/O + nopwrite does not panic when
#	rewriting identical data to a cloned zvol.
#
# STRATEGY:
#	1. Create a zvol with compress=on, checksum=sha256
#	2. Write data to the zvol via DIO (vol_dio_enabled=1)
#	3. Snapshot and clone the zvol
#	4. Write the same data to the clone via DIO
#	5. If the bug is present, this panics on:
#	   VERIFY3U(BP_GET_CHECKSUM(bp), ==, zp->zp_checksum)
#

verify_runnable "global"

if ! is_physical_device $DISKS; then
	log_unsupported "Cannot run on raw files."
fi

if ! tunable_exists VOL_DIO_ENABLED ; then
	log_unsupported "VOL_DIO_ENABLED tunable not available"
fi

typeset origin="$TESTPOOL/testvol.nopwrite"
typeset clone="$TESTPOOL/clone.nopwrite"
typeset vol_origin="${ZVOL_DEVDIR}/$origin"
typeset vol_clone="${ZVOL_DEVDIR}/$clone"
typeset datafile="$(mktemp -t zvol_nopwrite_data.XXXXXX)"

function cleanup
{
	datasetexists $clone && destroy_dataset $clone
	datasetexists $origin@snap && zfs destroy $origin@snap
	datasetexists $origin && destroy_dataset $origin
	rm -f "$datafile"
}
log_onexit cleanup

log_assert "Verify zvol DIO + nopwrite does not panic on cloned zvol"

# Ensure DIO is enabled
log_must set_tunable32 VOL_DIO_ENABLED 1

# Create origin zvol with nopwrite-compatible properties
log_must zfs create -b 128k -V 256M -o compression=on \
    -o checksum=sha256 $origin
block_device_wait

# Phase 1: Write known data to origin via DIO
log_note "Phase 1: Writing 64M to origin zvol via DIO"
log_must dd if=/dev/urandom of="$datafile" bs=1M count=64
log_must dd if="$datafile" of="$vol_origin" bs=1M count=64 conv=fsync

# Snapshot the origin
log_must zfs snapshot $origin@snap

# Clone the snapshot
log_must zfs clone -o compression=on -o checksum=sha256 $origin@snap $clone
block_device_wait

# Phase 2: Write the SAME data to the clone via DIO.
# This triggers nopwrite: the clone's blocks reference the origin's blocks
# (SHA256 checksum).  Writing identical data with checksum=sha256,
# compression=on causes zio_nop_write to succeed in open context,
# setting ZIO_FLAG_NOPWRITE and dr_nopwrite=TRUE.
#
# In syncing context, dbuf_sync_leaf sees data==NULL (DIO has no ARC
# buffer), sets WP_NOFILL -> checksum=OFF(2).  But the override bp has
# SHA256(8).  Without the fix, this panics:
#   VERIFY3U(BP_GET_CHECKSUM(bp), ==, zp->zp_checksum)  ->  8 == 2
#
log_note "Phase 2: Writing same data to clone via DIO"
log_must dd if="$datafile" of="$vol_clone" bs=1M count=64 conv=fsync

sync_pool

# If we got here without panicking, the fix works.
log_pass "zvol DIO + nopwrite completed without panic"
