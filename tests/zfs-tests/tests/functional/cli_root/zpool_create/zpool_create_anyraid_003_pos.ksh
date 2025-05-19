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
	log_must zpool create $TESTPOOL anyraid$parity $Cdisks $Adisks $Bdisks
	log_must poolexists $TESTPOOL
	destroy_pool $TESTPOOL
done

log_pass "'zpool create <pool> anyraid* ...' can create a pool with disks of various sizes."
