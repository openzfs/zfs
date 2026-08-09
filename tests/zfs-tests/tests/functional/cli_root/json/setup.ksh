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
