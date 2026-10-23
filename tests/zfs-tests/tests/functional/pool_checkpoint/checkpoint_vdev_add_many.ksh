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
# Copyright (c) 2026 Tigerblue77
#

. $STF_SUITE/include/libtest.shlib

#
# DESCRIPTION:
#	Rewinding to a checkpoint succeeds, and leaves an importable pool,
#	after more top-level vdevs were added than the pool had when the
#	checkpoint was taken.  The rewound uberblock must be written to
#	vdevs that the checkpointed pool has.
#
# STRATEGY:
#	1. Create a pool with one disk and take a checkpoint
#	2. Add eight disks
#	3. Rewind to the checkpoint
#	4. Verify that only the first disk is left, and that the pool can
#	   be exported and imported again
#	5. Repeat: the vdevs that get the rewound uberblock are picked at
#	   random, so that a single rewind may not show the problem
#

verify_runnable "global"

typeset -i ADDED=8
typeset -i ROUNDS=5
typeset VDEV_DIR="$TEST_BASE_DIR/ckpt_add_many"

function cleanup
{
	poolexists $TESTPOOL1 && destroy_pool $TESTPOOL1
	rm -rf $VDEV_DIR
}

log_onexit cleanup

log_must mkdir -p $VDEV_DIR
for i in $(seq 0 $ADDED); do
	log_must truncate -s $MINVDEVSIZE $VDEV_DIR/vdev.$i
done

for round in $(seq 1 $ROUNDS); do
	log_must zpool create -f $TESTPOOL1 $VDEV_DIR/vdev.0
	log_must zpool checkpoint $TESTPOOL1
	for i in $(seq 1 $ADDED); do
		log_must zpool add -f $TESTPOOL1 $VDEV_DIR/vdev.$i
	done

	log_must zpool export $TESTPOOL1
	log_must zpool import -d $VDEV_DIR --rewind-to-checkpoint $TESTPOOL1
	log_must eval "zpool list -v $TESTPOOL1 | grep -q 'vdev\.0'"
	log_mustnot eval "zpool list -v $TESTPOOL1 | grep -q 'vdev\.1'"

	log_must zpool export $TESTPOOL1
	log_must zpool import -d $VDEV_DIR $TESTPOOL1
	log_must zpool destroy $TESTPOOL1
	for i in $(seq 0 $ADDED); do
		log_must zpool labelclear -f $VDEV_DIR/vdev.$i
	done
done

log_pass "Rewind to checkpoint after adding many vdevs succeeds."
