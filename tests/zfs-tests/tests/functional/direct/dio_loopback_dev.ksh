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
# Copyright (c) 2025 by Triad National Security, LLC.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/direct/dio.cfg
. $STF_SUITE/tests/functional/direct/dio.kshlib

#
# DESCRIPTION:
# 	Verify Direct I/O reads work with loopback devices using direct=always.
#
# STRATEGY:
#	1. Create raidz zpool.
#	2. Create dataset with the direct dataset property set to always.
#	3. Create an empty file in dataset and setup loop device on it.
#	4. Read from loopback device.
#

verify_runnable "global"

function cleanup
{
	if [[ -n $lofidev ]]; then
		losetup -d $lofidev
	fi
	dio_cleanup
}

log_assert "Verify loopback devices with Direct I/O."

if ! is_linux; then
	log_unsupported "This is just a check for Linux Direct I/O"
fi

log_onexit cleanup

# Create zpool
log_must truncate -s $MINVDEVSIZE $DIO_VDEVS
log_must create_pool $TESTPOOL1 "raidz" $DIO_VDEVS

# Creating dataset with direct=always
log_must eval "zfs create -o direct=always $TESTPOOL1/$TESTFS1"
mntpt=$(get_prop mountpoint $TESTPOOL1/$TESTFS1)

# Getting a loopback device
lofidev=$(losetup -f)

# Create loopback device
log_must truncate -s 1M "$mntpt/temp_file"
log_must losetup $lofidev "$mntpt/temp_file"

# Read from looback device to make sure Direct I/O works with loopback device
log_must dd if=$lofidev of=/dev/null count=1 bs=4k

log_pass "Verified loopback devices for Direct I/O." 
