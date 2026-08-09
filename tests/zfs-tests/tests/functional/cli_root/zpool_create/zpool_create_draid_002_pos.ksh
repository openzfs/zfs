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
# Copyright (c) 2020 Lawrence Livermore National Security, LLC.

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
# Create dRAID pool using the maximum number of vdevs (255).  Then verify
# that creating a pool with 256 fails as expected.
#
# STRATEGY:
# 1) Verify a pool with fewer than the required vdevs fails.
# 2) Verify pools with a valid number of vdevs succeed.
# 3) Verify a pool which exceeds the maximum number of vdevs fails.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL

	rm -f $all_vdevs
	rmdir $TESTDIR
}

log_assert "'zpool create <pool> draid <vdevs>'"

log_onexit cleanup

all_vdevs=$(echo $TESTDIR/file.{01..256})

mkdir $TESTDIR
log_must truncate -s $MINVDEVSIZE $all_vdevs

# Below maximum dRAID vdev count for specified parity level.
log_mustnot zpool create $TESTPOOL draid1 $(echo $TESTDIR/file.{01..01})
log_mustnot zpool create $TESTPOOL draid2 $(echo $TESTDIR/file.{01..02})
log_mustnot zpool create $TESTPOOL draid3 $(echo $TESTDIR/file.{01..03})

# Verify pool sizes from 2-10.  Values in between are skipped to speed
# up the test case but will be exercised by the random pool creation
# done in zpool_create_draid_002_pos.ksh.
for (( i=2; i<=10; i++ )); do
	log_must zpool create $TESTPOOL draid:${i}c \
	    $(echo $TESTDIR/file.{01..$i})
	log_must destroy_pool $TESTPOOL
done

# Verify pool sizes from 254-255.
for (( i=254; i<=255; i++ )); do
	log_must zpool create $TESTPOOL draid:${i}c \
	    $(echo $TESTDIR/file.{01..$i})
	log_must destroy_pool $TESTPOOL
done

# Exceeds maximum dRAID vdev count (256).
log_mustnot zpool create $TESTPOOL draid $(echo $TESTDIR/file.{01..256})

log_pass "'zpool create <pool> draid <vdevs>'"
