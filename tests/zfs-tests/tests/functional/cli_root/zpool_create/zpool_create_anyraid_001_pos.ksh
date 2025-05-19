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
# Create a variety of AnyRAID pools using the minimal vdev syntax.
#
# STRATEGY:
# 1. Create the required number of allowed AnyRAID vdevs.
# 2. Create few pools of various sizes using the anyraid* syntax.
#

verify_runnable "global"

function cleanup
{
	poolexists $TESTPOOL && destroy_pool $TESTPOOL
}

log_assert "'zpool create <pool> <anyraid|0|1|2|3> ...' can create a pool."
log_onexit cleanup

create_sparse_files "disk" 4 $MINVDEVSIZE2

# Verify the default parity
log_must zpool create $TESTPOOL anyraid $disks
log_must poolexists $TESTPOOL
destroy_pool $TESTPOOL

# Verify specified parity
for parity in {0..3}; do
	log_must zpool create $TESTPOOL anyraid$parity $disks
	log_must poolexists $TESTPOOL
	destroy_pool $TESTPOOL
done

log_pass "'zpool create <pool> <anyraid|0|1|2|3> ...' can create a pool."
