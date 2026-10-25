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

. $STF_SUITE/tests/functional/cli_root/zpool_import/zpool_import.kshlib

#
# DESCRIPTION:
#	A device that keeps the label of a top-level vdev the pool no
#	longer has does not take the place of the vdev that now has its id,
#	when the pool is imported by scanning devices whose paths changed.
#
# STRATEGY:
#	1. Create a pool with two disks and take a checkpoint
#	2. Add a third disk, then rewind to the checkpoint: the disk is no
#	   longer part of the pool, but keeps its label
#	3. Add a fourth disk, which gets the same top-level vdev id
#	4. Export the pool and rename all four devices, so that the paths
#	   in the pool's config lead nowhere
#	5. Verify that the pool imports, healthy, with the fourth disk
#	6. Repeat with the stale device's new name sorting before and after
#	   the others
#

verify_runnable "global"

log_onexit cleanup

function test_stale_label
{
	typeset stale_name="$1"

	log_note "$0: stale device renamed to $stale_name"
	log_must zpool create $TESTPOOL1 $VDEV0 $VDEV1
	log_must zpool checkpoint $TESTPOOL1
	log_must zpool add $TESTPOOL1 $VDEV2
	log_must zpool export $TESTPOOL1
	log_must zpool import -d $DEVICE_DIR --rewind-to-checkpoint $TESTPOOL1
	log_must zpool add $TESTPOOL1 $VDEV3
	log_must zpool export $TESTPOOL1

	log_must mv $VDEV0 $DEVICE_DIR/m0
	log_must mv $VDEV1 $DEVICE_DIR/m1
	log_must mv $VDEV2 $DEVICE_DIR/$stale_name
	log_must mv $VDEV3 $DEVICE_DIR/m3

	log_must zpool import -d $DEVICE_DIR $TESTPOOL1
	log_must check_pool_healthy $TESTPOOL1
	log_must eval "zpool status $TESTPOOL1 | grep -q '$DEVICE_DIR/m3'"
	log_mustnot eval "zpool status $TESTPOOL1 | grep -q " \
	    "'$DEVICE_DIR/$stale_name'"

	cleanup
}

test_stale_label a2
test_stale_label z2

log_pass "A stale top-level vdev label does not replace the current vdev."
