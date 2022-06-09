#!/bin/ksh -p
#
# CDDL HEADER START
#
# The contents of this file are subject to the terms of the
# Common Development and Distribution License (the "License").
# You may not use this file except in compliance with the License.
#
# You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
# or http://www.opensolaris.org/os/licensing.
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
. $STF_SUITE/tests/functional/zvol/zvol_common.shlib

#
# DESCRIPTION:
#	Verify that a zvol Force Unit Access (FUA) write works.
#
# STRATEGY:
# 1. dd write 5MB of data with "oflag=dsync,direct" to a zvol.  Those flags
#    together do a FUA write.
# 3. Verify the data is correct.
# 3. Repeat 1-2 for both the blk-mq and non-blk-mq cases.

verify_runnable "global"

if ! is_physical_device $DISKS; then
	log_unsupported "This directory cannot be run on raw files."
fi

if ! is_linux ; then
	log_unsupported "Only linux supports dd with oflag=dsync for FUA writes"
fi

typeset datafile1="$(mktemp zvol_misc_fua1.XXXXXX)"
typeset datafile2="$(mktemp zvol_misc_fua2.XXXXXX)"
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

	# Write the data to our zvol using FUA
	log_must dd if=$datafile1 of=$zvolpath oflag=dsync,direct bs=1M count=5

	# Extract data from our zvol
	log_must dd if=$zvolpath of="$datafile2" bs=1M count=5

	# Compare the data we expect with what's on our zvol.  diff will return
	# non-zero if they differ.
	log_must diff $datafile1 $datafile2

	log_must rm $datafile1 $datafile2
}

log_assert "Verify that a ZFS volume can do Force Unit Access (FUA)"
log_onexit cleanup

log_must zfs set compression=off $TESTPOOL/$TESTVOL

log_note "Testing without blk-mq"

set_blk_mq 0
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
do_test

set_blk_mq 1
log_must zpool export $TESTPOOL
log_must zpool import $TESTPOOL
do_test

log_pass "ZFS volume FUA works"
