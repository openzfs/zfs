#!/bin/ksh -p
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
# Create a variety of dRAID pools using the minimal dRAID vdev syntax.
#
# STRATEGY:
# 1) Create the required number of allowed dRAID vdevs.
# 2) Create few pools of various sizes using the draid1|draid2|draid3 syntax.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL

	rm -f $all_vdevs
	rmdir $TESTDIR
}

log_assert "'zpool create <pool> <draid1|2|3> ...' can create a pool."

log_onexit cleanup

all_vdevs=$(echo $TESTDIR/file.{01..84})

mkdir $TESTDIR
log_must truncate -s $MINVDEVSIZE $all_vdevs

# Verify all configurations up to 24 vdevs.
for parity in {1..3}; do
	for children in {$((parity + 2))..24}; do
		vdevs=$(echo $TESTDIR/file.{01..${children}})
		log_must zpool create $TESTPOOL draid$parity $vdevs
		log_must poolexists $TESTPOOL
		destroy_pool $TESTPOOL
	done
done

# Spot check a few large configurations.
children_counts="53 84"
for children in $children_counts; do
	vdevs=$(echo $TESTDIR/file.{01..${children}})
	log_must zpool create $TESTPOOL draid $vdevs
	log_must poolexists $TESTPOOL
	destroy_pool $TESTPOOL
done

log_pass "'zpool create <pool> <draid1|2|3> <vdevs> ...' success."
