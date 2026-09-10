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
. $STF_SUITE/tests/functional/cli_root/zpool_create/zpool_create.shlib

#
# DESCRIPTION:
# Verify that AnyRAID vdev can be created using disks of different sizes.
#
# STRATEGY:
# 1. Create a pool using disks of different sizes.
# 2. Verify the pool created successfully.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "'zpool create <pool> anyraid* ...' can create a pool with disks of various sizes."
log_onexit cleanup

create_sparse_files "Adisk" 3 $(( $MINVDEVSIZE2 * 1 ))
create_sparse_files "Bdisk" 2 $(( $MINVDEVSIZE2 * 2 ))
create_sparse_files "Cdisk" 1 $(( $MINVDEVSIZE2 * 3 ))
ls -lh $Adisks $Bdisks $Cdisks

for parity in {0..3}; do
	log_must zpool create $TESTPOOL anymirror$parity $Cdisks $Adisks $Bdisks
	log_must poolexists $TESTPOOL
	destroy_pool $TESTPOOL
done

log_pass "'zpool create <pool> anyraid* ...' can create a pool with disks of various sizes."
