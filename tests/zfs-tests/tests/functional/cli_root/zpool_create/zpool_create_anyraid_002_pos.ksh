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
# https://opensource.org/license/CDDL-1.0.
#

#
# Copyright (c) 2025, Klara, Inc.
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Create AnyRAID pool using the maximum number of vdevs (255).  Then verify
# that creating a pool with 256 fails as expected.
#
# STRATEGY:
# 1. Verify a pool with fewer than the required vdevs fails.
# 2. Verify pools with a valid number of vdevs succeed.
# 3. Verify a pool which exceeds the maximum number of vdevs fails.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL

	log_pos rm -f $all_vdevs
	log_pos rmdir $TESTDIR
}

log_assert "'zpool create <pool> anyraid ...' can create a pool with maximum number of vdevs."
log_onexit cleanup

all_vdevs=$(echo $TESTDIR/file.{01..256})

mkdir $TESTDIR
log_must truncate -s $MINVDEVSIZE2 $all_vdevs

# Verify pool sizes from 254-255.
for (( i=254; i<=255; i++ )); do
	log_must zpool create $TESTPOOL anymirror3 \
	    $(echo $TESTDIR/file.{01..$i})
	log_must destroy_pool $TESTPOOL
done

# Exceeds maximum AnyRAID vdev count (256).
log_mustnot zpool create $TESTPOOL anymirror3 $(echo $TESTDIR/file.{01..256})

log_pass "'zpool create <pool> anyraid ...' can create a pool with maximum number of vdevs."
