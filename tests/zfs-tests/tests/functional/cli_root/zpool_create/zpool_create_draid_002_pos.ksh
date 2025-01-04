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
