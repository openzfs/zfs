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

	rm -f $all_vdevs
	rmdir $TESTDIR
}

log_assert "'zpool create <pool> anyraid ...' can create a pool with maximum number of vdevs."
log_onexit cleanup

all_vdevs=$(echo $TESTDIR/file.{01..256})

mkdir $TESTDIR
log_must truncate -s $MINVDEVSIZE2 $all_vdevs

# Verify pool sizes from 254-255.
for (( i=254; i<=255; i++ )); do
	log_must zpool create $TESTPOOL anyraid3 \
	    $(echo $TESTDIR/file.{01..$i})
	log_must destroy_pool $TESTPOOL
done

# Exceeds maximum AnyRAID vdev count (256).
log_mustnot zpool create $TESTPOOL anyraid3 $(echo $TESTDIR/file.{01..256})

log_pass "'zpool create <pool> anyraid ...' can create a pool with maximum number of vdevs."
