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
# http://www.illumos.org/license/CDDL.
#

#
# Copyright (c) 2025 ConnectWise Inc. All rights reserved.
#

. $STF_SUITE/include/libtest.shlib
. $STF_SUITE/tests/functional/cli_root/zpool_scrub/zpool_scrub.cfg

#
# DESCRIPTION:
# 'zpool scrub' with zfs_scrub_decompress set works
#
# STRATEGY:
# 1  Set zfs_scrub_decompress tunable.
# 2. Start a scrub and wait for it to finish.
# 3. Repeat this with zfs_scrub_decompress not set.
#

function cleanup
{
	set_tunable32 SCRUB_DECOMPRESS 0
	zfs destroy $TESTPOOL/newfs
}

verify_runnable "global"

log_onexit cleanup

log_assert "Scrub with decompression tunable set - works."

# Create out testing dataset
log_must zfs create $TESTPOOL/newfs
# Make sure compression is on
log_must zfs set compression=on $TESTPOOL/newfs
typeset file="/$TESTPOOL/newfs/$TESTFILE0"
# Create some data in our dataset
log_must dd if=/dev/urandom of=$file bs=1024 count=1024 oflag=sync
# Make sure data is compressible
log_must eval "echo 'aaaaaaaa' >> "$file

# Enable decompression of blocks read by scrub
log_must set_tunable32 SCRUB_DECOMPRESS 1
# Run and wait for scrub
log_must zpool scrub -w $TESTPOOL

# Disable decompression of blocks read by scrub
log_must set_tunable32 SCRUB_DECOMPRESS 0
# Run and wait for scrub
log_must zpool scrub -w $TESTPOOL

log_pass "Scrub with decompression tunable set - works."
