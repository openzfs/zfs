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
# Create a variety of AnyRAID pools using the minimal vdev syntax.
#
# STRATEGY:
# 1. Create the required number of allowed vdevs.
# 2. Create few pools of various sizes using the anymirror* syntax.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "'zpool create <pool> <anymirror|0|1|2|3> ...' can create a pool."
log_onexit cleanup

create_sparse_files "disk" 7 $MINVDEVSIZE2

# Verify the default parity
log_must zpool create $TESTPOOL anymirror $disks
log_must poolexists $TESTPOOL
destroy_pool $TESTPOOL

# Verify specified parity
for parity in {0..6}; do
	log_must zpool create $TESTPOOL anymirror$parity $disks
	log_must poolexists $TESTPOOL
	destroy_pool $TESTPOOL
done

log_pass "'zpool create <pool> <anymirror|0|1|2|3> ...' can create a pool."
