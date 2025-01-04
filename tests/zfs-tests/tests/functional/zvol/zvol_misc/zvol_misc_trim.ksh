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
# Copyright (c) 2022 by Lawrence Livermore National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/include/math.shlib
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib

#
# DESCRIPTION:
#	Verify we can TRIM a zvol
#
# STRATEGY:
# 1. TRIM the entire zvol to remove data from older tests
# 2. Create a 5MB data file
# 3. Write the file to the zvol
# 4. Observe 5MB of used space on the zvol
# 5. TRIM the first 1MB and last 2MB of the 5MB block of data.
# 6. Observe 2MB of used space on the zvol
# 7. Verify the trimmed regions are zero'd on the zvol

verify_runnable "global"

if is_linux ; then
	# We need '--force' here since the prior tests may leave a filesystem
	# on the zvol, and blkdiscard will see that filesystem and print a
	# warning unless you force it.
	#
	# Only blkdiscard >= v2.36 supports --force, so we need to
	# check for it.
	if blkdiscard --help | grep -q '\-\-force' ; then
		trimcmd='blkdiscard --force'
	else
		trimcmd='blkdiscard'
	fi
else
	# By default, FreeBSD 'trim' always does a dry-run.  '-f' makes
	# it perform the actual operation.
	trimcmd='trim -f'
fi

if ! is_physical_device $DISKS; then
	log_unsupported "This directory cannot be run on raw files."
fi

typeset datafile1="$(mktemp zvol_misc_flags1.XXXXXX)"
typeset datafile2="$(mktemp zvol_misc_flags2.XXXXXX)"
typeset zvolpath=${ZVOL_DEVDIR}/$TESTPOOL/$TESTVOL

function cleanup
{
       rm "$datafile1" "$datafile2"
}

function do_test {
	# Wait for udev to create symlinks to our zvol
	block_device_wait $zvolpath

	# Create a data file
	log_must dd if=/dev/urandom of="$datafile1" bs=1M count=5
	
	# Write to zvol
	log_must dd if=$datafile1 of=$zvolpath conv=fsync
	sync_pool

	# Record how much space we've used (should be 5MB, with 128k
	# of tolerance).
	before="$(get_prop refer $TESTPOOL/$TESTVOL)"
	log_must within_tolerance $before 5242880 131072

	# We currently have 5MB of random data on the zvol.
	# Trim the first 1MB and also trim 2MB at offset 3MB.
	log_must $trimcmd -l $((1 * 1048576)) $zvolpath
	log_must $trimcmd -o $((3 * 1048576)) -l $((2 * 1048576)) $zvolpath
	sync_pool

	# After trimming 3MB, the zvol should have 2MB of data (with 128k of
	# tolerance).
	after="$(get_prop refer $TESTPOOL/$TESTVOL)"
	log_must within_tolerance $after 2097152 131072

	# Make the same holes in our test data
	log_must dd if=/dev/zero of="$datafile1" bs=1M count=1 conv=notrunc
	log_must dd if=/dev/zero of="$datafile1" bs=1M count=2 seek=3 conv=notrunc

	# Extract data from our zvol
	log_must dd if=$zvolpath of="$datafile2" bs=1M count=5

	# Compare the data we expect with what's on our zvol.  diff will return
	# non-zero if they differ.
	log_must diff $datafile1 $datafile2

	log_must rm $datafile1 $datafile2
}

log_assert "Verify that a ZFS volume can be TRIMed"
log_onexit cleanup

log_must zfs set compression=off $TESTPOOL/$TESTVOL

# Remove old data from previous tests
log_must $trimcmd $zvolpath

set_blk_mq 1
log_must_busy zpool export $TESTPOOL
log_must zpool import $TESTPOOL
do_test

set_blk_mq 0
log_must_busy zpool export $TESTPOOL
log_must zpool import $TESTPOOL
do_test

log_pass "ZFS volumes can be trimmed"
