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

# Copyright (c) 2024 by Lawrence Livermore National Security, LLC.

. $STF_SUITE/include/libtest.shlib

# Sanity check that 'testpool1' or 'testpool2' don't exist
log_mustnot zpool status -j | \
	jq -e '.pools | has("testpool1") or has("testpool2")' &> /dev/null

mkdir -p $TESTDIR
truncate -s 80M $TESTDIR/file{1..28}

DISK=${DISKS%% *}

# Create complex pool configs to exercise JSON
zpool create -f testpool1 draid $TESTDIR/file{1..10} \
	special $DISK \
	dedup $TESTDIR/file11 \
	special $TESTDIR/file12 \
	cache $TESTDIR/file13 \
	log $TESTDIR/file14

zpool create -f testpool2 mirror $TESTDIR/file{15,16} \
	raidz1 $TESTDIR/file{17,18,19} \
	cache $TESTDIR/file20 \
	log $TESTDIR/file21 \
	special mirror $TESTDIR/file{22,23} \
	dedup mirror $TESTDIR/file{24,25} \
	spare $TESTDIR/file{26,27,28}
