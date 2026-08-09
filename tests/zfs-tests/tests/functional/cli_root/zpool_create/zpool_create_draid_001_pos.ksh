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
